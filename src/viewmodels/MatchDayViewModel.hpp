#pragma once

#include "core/algorithms/LineupValidator.hpp"
#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/MatchNote.hpp"
#include "core/util/Error.hpp"
#include "services/MatchDayService.hpp"
#include "viewmodels/MatchDayModels.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class Clock;
class LineupService;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The Match Day screen (SPEC section 4, packet P13): the clock derived from the persisted steps
// (QTimer only repaints it), the score, the players as tiles, the events and notes, and undo.
// Recording an action picks its players in steps (beginAction → choosePlayer…), so every action is
// reachable by clicks or taps; each has a minute field, the current minute until it is edited, and
// may be entered late at any minute already played. The service writes each one at once. Without
// RunMatchDay the match is read-only.
class MatchDayViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no team. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    // The view binds it to AppSettings.yellowCardThreshold (the start checks the lineup).
    Q_PROPERTY(int yellowCardThreshold READ yellowCardThreshold WRITE setYellowCardThreshold NOTIFY
                   yellowCardThresholdChanged FINAL)

    // The match
    // 0: no match is open.
    Q_PROPERTY(qint64 matchId READ matchId NOTIFY matchChanged FINAL)
    Q_PROPERTY(bool hasMatch READ hasMatch NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString opponentName READ opponentName NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString matchDetailsText READ matchDetailsText NOTIFY textsChanged FINAL)
    // planned, live, finished; empty without a match.
    Q_PROPERTY(QString statusKey READ statusKey NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY textsChanged FINAL)
    // "2 × 45 min"; empty before the start.
    Q_PROPERTY(QString formatText READ formatText NOTIFY textsChanged FINAL)
    // The match is live and the user may run it: leaving asks first.
    Q_PROPERTY(bool guardLeaving READ guardLeaving NOTIFY matchChanged FINAL)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY matchChanged FINAL)
    // Why the screen is read-only; empty when it is not.
    Q_PROPERTY(QString readOnlyText READ readOnlyText NOTIFY textsChanged FINAL)

    // Start
    // "" (can start), notPlanned, anotherLive, noLineup, lineupErrors; empty when not planned.
    Q_PROPERTY(QString startBlock READ startBlock NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString startBlockText READ startBlockText NOTIFY textsChanged FINAL)
    Q_PROPERTY(bool canStart READ canStart NOTIFY matchChanged FINAL)
    // [{ value: "2x45", text: "2 × 45 min" }…] the presets, then { value: "custom" }.
    Q_PROPERTY(QVariantList formatOptions READ formatOptions NOTIFY textsChanged FINAL)
    // The preset of the team category ("2x45").
    Q_PROPERTY(QString defaultFormat READ defaultFormat NOTIFY matchChanged FINAL)
    Q_PROPERTY(int minPeriods READ minPeriods CONSTANT FINAL)
    Q_PROPERTY(int maxPeriods READ maxPeriods CONSTANT FINAL)
    Q_PROPERTY(int minPeriodLength READ minPeriodLength CONSTANT FINAL)
    Q_PROPERTY(int maxPeriodLength READ maxPeriodLength CONSTANT FINAL)

    // The clock
    // notStarted, running, break, finished; empty before Match Day starts.
    Q_PROPERTY(QString phase READ phase NOTIFY clockChanged FINAL)
    Q_PROPERTY(QString phaseText READ phaseText NOTIFY clockChanged FINAL)
    // "23:41", in stoppage time "45+2:10".
    Q_PROPERTY(QString clockText READ clockText NOTIFY clockChanged FINAL)
    // What the clock button does next ("Start 1st half"…); empty after the end.
    Q_PROPERTY(QString clockActionText READ clockActionText NOTIFY clockChanged FINAL)
    Q_PROPERTY(bool canAdvanceClock READ canAdvanceClock NOTIFY clockChanged FINAL)
    // Back to planned: before the kick-off only.
    Q_PROPERTY(bool canCancelStart READ canCancelStart NOTIFY clockChanged FINAL)
    // Goals, cards and substitutions: a part runs or a break (late entry), and 7 players or more.
    Q_PROPERTY(bool canRecordEvents READ canRecordEvents NOTIFY clockChanged FINAL)
    Q_PROPERTY(bool canSubstitute READ canSubstitute NOTIFY clockChanged FINAL)
    // Notes: a part runs or a break, also with fewer than 7 players.
    Q_PROPERTY(bool canAddNote READ canAddNote NOTIFY clockChanged FINAL)

    // Players on the pitch (P13: at least 7 by the laws of the game)
    Q_PROPERTY(int onPitchCount READ onPitchCount NOTIFY eventsChanged FINAL)
    Q_PROPERTY(int minPlayers READ minPlayers CONSTANT FINAL)
    // A live match with fewer than 7: only the end of the match, notes and undo.
    Q_PROPERTY(bool tooFewPlayers READ tooFewPlayers NOTIFY eventsChanged FINAL)

    // Score and events
    Q_PROPERTY(int goalsFor READ goalsFor NOTIFY eventsChanged FINAL)
    Q_PROPERTY(int goalsAgainst READ goalsAgainst NOTIFY eventsChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY eventsChanged FINAL)
    // What undo takes back: "Goal · Jan Kowalski 23'"; empty when nothing.
    Q_PROPERTY(QString undoText READ undoText NOTIFY eventsChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::MatchDayPlayersModel* pitch READ pitch CONSTANT FINAL)
    // The bench, then the players who left the pitch.
    Q_PROPERTY(fdss::viewmodels::MatchDayPlayersModel* bench READ bench CONSTANT FINAL)
    // Events and notes, the latest in the match first.
    Q_PROPERTY(fdss::viewmodels::MatchTimelineModel* timeline READ timeline CONSTANT FINAL)
    // [{ value: playerId or 0, text }]: "No player", then the players of the match, for a note.
    Q_PROPERTY(QVariantList notePlayerOptions READ notePlayerOptions NOTIFY eventsChanged FINAL)

    // The action being recorded
    // "", scorer, assist, yellow, red, subOff, subOn, opponentGoal.
    Q_PROPERTY(QString pendingAction READ pendingAction NOTIFY pendingChanged FINAL)
    // "Who scored?"; empty without a pending action.
    Q_PROPERTY(QString pendingPrompt READ pendingPrompt NOTIFY pendingChanged FINAL)
    // The minute of the pending action in the field: the current one ("23", "45+2") until edited.
    Q_PROPERTY(QString pendingMinute READ pendingMinute NOTIFY pendingMinuteChanged FINAL)
    // Why the minute cannot be used; empty when it can.
    Q_PROPERTY(QString pendingMinuteError READ pendingMinuteError NOTIFY pendingMinuteChanged FINAL)

    // Why the match could not be read; the view offers to try again. Empty: it is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    explicit MatchDayViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] int yellowCardThreshold() const;
    void setYellowCardThreshold(int threshold);

    [[nodiscard]] qint64 matchId() const;
    [[nodiscard]] bool hasMatch() const;
    [[nodiscard]] QString opponentName() const;
    [[nodiscard]] QString matchDetailsText() const;
    [[nodiscard]] QString statusKey() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString formatText() const;
    [[nodiscard]] bool guardLeaving() const;
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] QString readOnlyText() const;

    [[nodiscard]] QString startBlock() const;
    [[nodiscard]] QString startBlockText() const;
    [[nodiscard]] bool canStart() const;
    [[nodiscard]] QVariantList formatOptions() const;
    [[nodiscard]] QString defaultFormat() const;
    [[nodiscard]] static int minPeriods();
    [[nodiscard]] static int maxPeriods();
    [[nodiscard]] static int minPeriodLength();
    [[nodiscard]] static int maxPeriodLength();

    [[nodiscard]] QString phase() const;
    [[nodiscard]] QString phaseText() const;
    [[nodiscard]] QString clockText() const;
    [[nodiscard]] QString clockActionText() const;
    [[nodiscard]] bool canAdvanceClock() const;
    [[nodiscard]] bool canCancelStart() const;
    [[nodiscard]] bool canRecordEvents() const;
    [[nodiscard]] bool canSubstitute() const;
    [[nodiscard]] bool canAddNote() const;

    [[nodiscard]] int onPitchCount() const;
    [[nodiscard]] static int minPlayers();
    [[nodiscard]] bool tooFewPlayers() const;

    [[nodiscard]] int goalsFor() const;
    [[nodiscard]] int goalsAgainst() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] MatchDayPlayersModel* pitch() const;
    [[nodiscard]] MatchDayPlayersModel* bench() const;
    [[nodiscard]] MatchTimelineModel* timeline() const;
    [[nodiscard]] QVariantList notePlayerOptions() const;

    [[nodiscard]] QString pendingAction() const;
    [[nodiscard]] QString pendingPrompt() const;
    [[nodiscard]] QString pendingMinute() const;
    [[nodiscard]] QString pendingMinuteError() const;
    [[nodiscard]] QString loadError() const;

    // Opens the match (navigationArgs.matchId).
    Q_INVOKABLE void openMatch(qint64 matchId);
    // The live match of the team, otherwise the next planned one, otherwise none.
    Q_INVOKABLE void openDefaultMatch();
    // Reads the open match again (the view becomes visible, Try again).
    Q_INVOKABLE void reload();

    // The actions return true when saved; otherwise errorOccurred says why.
    // A preset ("2x45") or "custom" with the parts and their length.
    Q_INVOKABLE bool start(const QString& format, int periods, int periodLength);
    Q_INVOKABLE bool cancelStart();
    Q_INVOKABLE bool advanceClock();
    // Finishes the match now (offered with fewer than 7 players).
    Q_INVOKABLE bool endMatch();
    // goal, opponentGoal, yellow, red, sub: the next taps choose the players. Anything else
    // cancels.
    Q_INVOKABLE void beginAction(const QString& action);
    // The minute typed in the field of the pending action.
    Q_INVOKABLE void setPendingMinute(const QString& text);
    // A player for the pending action; the last one saves it.
    Q_INVOKABLE bool choosePlayer(qint64 playerId);
    // Saves the pending goal without an assist.
    Q_INVOKABLE bool skipAssist();
    // Saves the pending opponent goal.
    Q_INVOKABLE bool confirmAction();
    Q_INVOKABLE void cancelAction();
    // The toast text naming what was undone; empty when nothing was.
    Q_INVOKABLE QString undo();

    // Notes. The minute field text of a new note: the last minute played.
    Q_INVOKABLE QString defaultNoteMinute() const;
    // Why a minute typed for a note cannot be used; empty when it can.
    Q_INVOKABLE QString noteMinuteError(const QString& text) const;
    // playerId 0: no player.
    Q_INVOKABLE bool addNote(const QString& minute, qint64 playerId, const QString& text);
    Q_INVOKABLE bool updateNote(qint64 noteId, const QString& minute, qint64 playerId,
                                const QString& text);
    // The undo toast text; restoreDeletedNote() brings it back. Empty when nothing was deleted.
    Q_INVOKABLE QString deleteNote(qint64 noteId);
    Q_INVOKABLE void restoreDeletedNote();

signals:
    void teamIdChanged();
    void yellowCardThresholdChanged();
    void matchChanged();
    void clockChanged();
    void eventsChanged();
    void pendingChanged();
    void pendingMinuteChanged();
    void loadErrorChanged();
    // Texts that follow the language.
    void textsChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);
    void notice(const QString& message);
    // An action left fewer than 7 players: the view asks whether to end the match.
    void tooFewPlayersReached();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Pending { None, Scorer, Assist, Yellow, Red, SubOff, SubOn, OpponentGoal };

    // Reads the match; on failure keeps nothing and sets the load error.
    void read(core::MatchId match);
    void clearMatch(std::optional<core::Error> failure);
    // After every action and every new minute.
    void rebuildRows();
    void updateTimer();
    void tick();
    void setPending(Pending pending, std::optional<core::PlayerId> first = std::nullopt);
    // After a write: reads again, tells the view, and checks the players left on the pitch.
    bool finish(const core::Status& status);
    void report(const core::Error& error);
    [[nodiscard]] bool checkRunnable();
    [[nodiscard]] std::optional<core::ClockReading> reading() const;
    [[nodiscard]] core::MatchPhase currentPhase() const;
    [[nodiscard]] bool isLive() const;
    [[nodiscard]] QString playerName(std::optional<core::PlayerId> player) const;
    [[nodiscard]] QString eventText(const core::MatchEvent& event, bool secondYellow) const;
    [[nodiscard]] QString actionText(const services::MatchDayAction& action) const;
    // Undo of the end refused because another match of the team is live: which one.
    [[nodiscard]] QString anotherLiveMatchText() const;
    // The time typed (or the current one while not edited); nullopt with `error` set.
    [[nodiscard]] std::optional<core::MatchTime> timeOf(const QString& text, bool substitution,
                                                        QString& error) const;
    [[nodiscard]] std::optional<core::MatchTime> pendingTime(QString& error) const;
    // Records an event of the pending action at its minute: first checks a late entry against the
    // later events, so a refusal names the event it conflicts with.
    bool record(core::MatchEventDraft event);

    services::MatchDayService& m_service;
    services::LineupService& m_lineupService;
    services::Session& m_session;
    const services::Clock& m_clock;
    MatchDayPlayersModel* m_pitch;
    MatchDayPlayersModel* m_bench;
    MatchTimelineModel* m_timeline;
    QTimer m_timer;

    qint64 m_teamId = 0;
    int m_yellowCardThreshold = core::kDefaultYellowCardThreshold;
    std::optional<services::MatchDayState> m_state;
    std::optional<services::MatchDayStartCheck> m_startCheck;
    std::optional<core::Error> m_loadFailure;
    Pending m_pending = Pending::None;
    // The scorer of a pending goal, or the player going off of a pending substitution.
    std::optional<core::PlayerId> m_pendingFirst;
    // The minute typed for the pending action; nullopt: the current one.
    std::optional<QString> m_pendingMinute;
    std::optional<core::MatchNote> m_deletedNote;
    // The minute the rows were built for; a new one rebuilds them.
    int m_rowsMinute = -1;
};

} // namespace fdss::viewmodels
