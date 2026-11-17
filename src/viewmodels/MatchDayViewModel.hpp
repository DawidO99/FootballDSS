#pragma once

#include "core/algorithms/LineupValidator.hpp"
#include "core/algorithms/MatchMinutes.hpp"
#include "core/util/Error.hpp"
#include "services/MatchDayService.hpp"
#include "viewmodels/MatchDayModels.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class Clock;
class LineupService;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The Match Day screen (SPEC section 4, packet P13): the clock derived from the persisted steps
// (QTimer only repaints it), the score, the players as tiles, the events and undo. Recording an
// action picks its players in steps (beginAction → choosePlayer…), so every action is reachable by
// clicks or taps; the service writes each one at once. Without RunMatchDay the match is read-only.
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
    Q_PROPERTY(int defaultHalfLength READ defaultHalfLength NOTIFY matchChanged FINAL)
    Q_PROPERTY(int minHalfLength READ minHalfLength CONSTANT FINAL)
    Q_PROPERTY(int maxHalfLength READ maxHalfLength CONSTANT FINAL)

    // The clock
    // notStarted, firstHalf, halfTime, secondHalf, finished; empty before Match Day starts.
    Q_PROPERTY(QString phase READ phase NOTIFY clockChanged FINAL)
    Q_PROPERTY(QString phaseText READ phaseText NOTIFY clockChanged FINAL)
    // "23:41", in stoppage time "45+2:10".
    Q_PROPERTY(QString clockText READ clockText NOTIFY clockChanged FINAL)
    // What the clock button does next ("Start 1st half"…); empty after the end.
    Q_PROPERTY(QString clockActionText READ clockActionText NOTIFY clockChanged FINAL)
    Q_PROPERTY(bool canAdvanceClock READ canAdvanceClock NOTIFY clockChanged FINAL)
    // Back to planned: before the kick-off only.
    Q_PROPERTY(bool canCancelStart READ canCancelStart NOTIFY clockChanged FINAL)
    // Goals and cards: a half is running.
    Q_PROPERTY(bool canRecordEvents READ canRecordEvents NOTIFY clockChanged FINAL)
    // Substitutions: a half is running, or the break.
    Q_PROPERTY(bool canSubstitute READ canSubstitute NOTIFY clockChanged FINAL)

    // Score and events
    Q_PROPERTY(int goalsFor READ goalsFor NOTIFY eventsChanged FINAL)
    Q_PROPERTY(int goalsAgainst READ goalsAgainst NOTIFY eventsChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY eventsChanged FINAL)
    // What undo takes back: "Goal · Jan Kowalski 23'"; empty when nothing.
    Q_PROPERTY(QString undoText READ undoText NOTIFY eventsChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::MatchDayPlayersModel* pitch READ pitch CONSTANT FINAL)
    // The bench, then the players who left the pitch.
    Q_PROPERTY(fdss::viewmodels::MatchDayPlayersModel* bench READ bench CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::MatchTimelineModel* timeline READ timeline CONSTANT FINAL)

    // The action being recorded
    // "", scorer, assist, yellow, red, subOff, subOn.
    Q_PROPERTY(QString pendingAction READ pendingAction NOTIFY pendingChanged FINAL)
    // "Who scored?"; empty without a pending action.
    Q_PROPERTY(QString pendingPrompt READ pendingPrompt NOTIFY pendingChanged FINAL)

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
    [[nodiscard]] bool guardLeaving() const;
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] QString readOnlyText() const;

    [[nodiscard]] QString startBlock() const;
    [[nodiscard]] QString startBlockText() const;
    [[nodiscard]] bool canStart() const;
    [[nodiscard]] int defaultHalfLength() const;
    [[nodiscard]] static int minHalfLength();
    [[nodiscard]] static int maxHalfLength();

    [[nodiscard]] QString phase() const;
    [[nodiscard]] QString phaseText() const;
    [[nodiscard]] QString clockText() const;
    [[nodiscard]] QString clockActionText() const;
    [[nodiscard]] bool canAdvanceClock() const;
    [[nodiscard]] bool canCancelStart() const;
    [[nodiscard]] bool canRecordEvents() const;
    [[nodiscard]] bool canSubstitute() const;

    [[nodiscard]] int goalsFor() const;
    [[nodiscard]] int goalsAgainst() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] MatchDayPlayersModel* pitch() const;
    [[nodiscard]] MatchDayPlayersModel* bench() const;
    [[nodiscard]] MatchTimelineModel* timeline() const;

    [[nodiscard]] QString pendingAction() const;
    [[nodiscard]] QString pendingPrompt() const;
    [[nodiscard]] QString loadError() const;

    // Opens the match (navigationArgs.matchId).
    Q_INVOKABLE void openMatch(qint64 matchId);
    // The live match of the team, otherwise the next planned one, otherwise none.
    Q_INVOKABLE void openDefaultMatch();
    // Reads the open match again (the view becomes visible, Try again).
    Q_INVOKABLE void reload();

    // The actions return true when saved; otherwise errorOccurred says why.
    Q_INVOKABLE bool start(int halfLength);
    Q_INVOKABLE bool cancelStart();
    Q_INVOKABLE bool advanceClock();
    // goal, yellow, red, sub: the next taps choose the players. Anything else cancels.
    Q_INVOKABLE void beginAction(const QString& action);
    // A player for the pending action; the last one saves it.
    Q_INVOKABLE bool choosePlayer(qint64 playerId);
    // Saves the pending goal without an assist.
    Q_INVOKABLE bool skipAssist();
    Q_INVOKABLE void cancelAction();
    Q_INVOKABLE bool recordOpponentGoal();
    // The toast text naming what was undone; empty when nothing was.
    Q_INVOKABLE QString undo();

signals:
    void teamIdChanged();
    void yellowCardThresholdChanged();
    void matchChanged();
    void clockChanged();
    void eventsChanged();
    void pendingChanged();
    void loadErrorChanged();
    // Texts that follow the language.
    void textsChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Pending { None, Scorer, Assist, Yellow, Red, SubOff, SubOn };

    // Reads the match; on failure keeps nothing and sets the load error.
    void read(core::MatchId match);
    void clearMatch(std::optional<core::Error> failure);
    // After every action and every new minute.
    void rebuildRows();
    void updateTimer();
    void tick();
    void setPending(Pending pending, std::optional<core::PlayerId> first = std::nullopt);
    // After a write: reads again and tells the view.
    bool finish(const core::Status& status);
    void report(const core::Error& error);
    [[nodiscard]] bool checkRunnable();
    [[nodiscard]] std::optional<core::ClockReading> reading() const;
    [[nodiscard]] core::MatchPhase currentPhase() const;
    [[nodiscard]] QString playerName(std::optional<core::PlayerId> player) const;
    [[nodiscard]] QString actionText(const services::MatchDayAction& action) const;
    // Undo of the end refused because another match of the team is live: which one.
    [[nodiscard]] QString anotherLiveMatchText() const;

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
    // The minute the rows were built for; a new one rebuilds them.
    int m_rowsMinute = -1;
};

} // namespace fdss::viewmodels
