#pragma once

#include "core/util/Error.hpp"
#include "services/MatchReportService.hpp"
#include "viewmodels/MatchDayModels.hpp"
#include "viewmodels/MatchReportModels.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <variant>
#include <vector>

namespace fdss::services {
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The match report (SPEC section 4, packet P14): the header, the timeline with the corrections, the
// shapes with their minutes, the ratings grid with rating and RPE, and the note. Ratings, RPE and
// the note are saved together (dirty until then); the corrections are saved at once, keep the
// unsaved ratings, and a deleted row comes back from the undo toast (deleted(), undoDelete()).
// Changes need EditLineup; other roles read.
class MatchReportViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no match is open.
    Q_PROPERTY(qint64 matchId READ matchId NOTIFY matchChanged FINAL)
    Q_PROPERTY(bool hasReport READ hasReport NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString opponentName READ opponentName NOTIFY matchChanged FINAL)
    // ISO yyyy-MM-dd, and as the app language shows it.
    Q_PROPERTY(QString date READ date NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString dateText READ dateText NOTIFY textsChanged FINAL)
    Q_PROPERTY(QString competition READ competition NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString venueText READ venueText NOTIFY textsChanged FINAL)
    // "2 × 45 min"; empty for a match that Match Day did not run.
    Q_PROPERTY(QString formatText READ formatText NOTIFY textsChanged FINAL)
    Q_PROPERTY(int goalsFor READ goalsFor NOTIFY matchChanged FINAL)
    Q_PROPERTY(int goalsAgainst READ goalsAgainst NOTIFY matchChanged FINAL)
    // win, draw, loss; empty without a result.
    Q_PROPERTY(QString resultKey READ resultKey NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString resultText READ resultText NOTIFY textsChanged FINAL)
    // The match is not finished (e.g. its end was undone in Match Day): no report to change.
    Q_PROPERTY(bool notFinished READ notFinished NOTIFY matchChanged FINAL)
    // Without EditLineup.
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString readOnlyText READ readOnlyText NOTIFY textsChanged FINAL)
    // Events can be added and deleted (P14 decision 2); notes, ratings and RPE always.
    Q_PROPERTY(bool correctable READ correctable NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString notCorrectableText READ notCorrectableText NOTIFY textsChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::MatchRatingsModel* ratings READ ratings CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::MatchTimelineModel* timeline READ timeline CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::MatchShapesModel* shapes READ shapes CONSTANT FINAL)
    // "12 of 14 rated".
    Q_PROPERTY(QString ratedText READ ratedText NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(QString note READ note NOTIFY noteChanged FINAL)
    Q_PROPERTY(int noteMaxLength READ noteMaxLength CONSTANT FINAL)
    // Ratings, RPE or the note changed since they were saved.
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    // [{ value, text }]: goal, opponentGoal, yellow, red, sub.
    Q_PROPERTY(QVariantList eventTypeOptions READ eventTypeOptions NOTIFY textsChanged FINAL)
    // [{ value: playerId or 0, text }] for a note.
    Q_PROPERTY(QVariantList notePlayerOptions READ notePlayerOptions NOTIFY matchChanged FINAL)
    // "90", the last minute of the match, for a new event or note.
    Q_PROPERTY(QString lastMinute READ lastMinute NOTIFY matchChanged FINAL)
    // Why the report could not be read; the view offers to try again. Empty: it is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    static constexpr qint64 kTenWindowMs = 600;

    explicit MatchReportViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 matchId() const;
    [[nodiscard]] bool hasReport() const;
    [[nodiscard]] QString opponentName() const;
    [[nodiscard]] QString date() const;
    [[nodiscard]] QString dateText() const;
    [[nodiscard]] QString competition() const;
    [[nodiscard]] QString venueText() const;
    [[nodiscard]] QString formatText() const;
    [[nodiscard]] int goalsFor() const;
    [[nodiscard]] int goalsAgainst() const;
    [[nodiscard]] QString resultKey() const;
    [[nodiscard]] QString resultText() const;
    [[nodiscard]] bool notFinished() const;
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] QString readOnlyText() const;
    [[nodiscard]] bool correctable() const;
    [[nodiscard]] QString notCorrectableText() const;
    [[nodiscard]] MatchRatingsModel* ratings() const;
    [[nodiscard]] MatchTimelineModel* timeline() const;
    [[nodiscard]] MatchShapesModel* shapes() const;
    [[nodiscard]] QString ratedText() const;
    [[nodiscard]] QString note() const;
    [[nodiscard]] int noteMaxLength() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] QVariantList eventTypeOptions() const;
    [[nodiscard]] QVariantList notePlayerOptions() const;
    [[nodiscard]] QString lastMinute() const;
    [[nodiscard]] QString loadError() const;

    // Drops changes that were not saved.
    Q_INVOKABLE void openMatch(qint64 matchId);
    // Reads the report again; ratings, RPE and the note the user changed stay.
    Q_INVOKABLE void reload();

    // The grid (MatchRatingsModel::Column). Digit 0–9; `1` then `0` within kTenWindowMs in the
    // same cell is 10. A lone 0 is not a rating: the row shows an error.
    Q_INVOKABLE void enterDigit(int row, int column, int digit);
    // Delete, Backspace.
    Q_INVOKABLE void clearCell(int row, int column);
    // A cell got the focus: this is what revertCell() goes back to.
    Q_INVOKABLE void rememberCell(int row, int column);
    // Esc.
    Q_INVOKABLE void revertCell(int row, int column);
    Q_INVOKABLE void setNote(const QString& text);
    // True when saved; otherwise errorOccurred, or the rows say why.
    Q_INVOKABLE bool save();
    Q_INVOKABLE void discardChanges();
    // The first row with an error, -1 when there is none.
    Q_INVOKABLE int firstErrorRow() const;

    // The corrections. A minute as on the timeline ("23", "45+2"); empty error: it is fine.
    Q_INVOKABLE QString minuteError(const QString& minute) const;
    // [{ value: playerId, text }] for an event to add at that minute. role "first": the scorer,
    // the player of a card, the player coming on; "second": the assistant (with "No assist"), the
    // player going off. Empty for a minute that cannot be read.
    Q_INVOKABLE QVariantList playerOptions(const QString& type, const QString& minute,
                                           const QString& role) const;
    // An empty text when the event fits; otherwise why, naming the later event it conflicts with.
    Q_INVOKABLE QString eventError(const QString& type, const QString& minute, qint64 player,
                                   qint64 related) const;
    Q_INVOKABLE bool addEvent(const QString& type, const QString& minute, qint64 player,
                              qint64 related);
    // Deleting it changes the score: the view asks first (AGENTS section 6).
    Q_INVOKABLE bool eventRemovesGoal(qint64 eventId) const;
    // Deletes at once, then deleted() for the undo toast. A later event that needs it:
    // errorOccurred naming that event, and nothing is deleted.
    Q_INVOKABLE bool deleteEvent(qint64 eventId);
    Q_INVOKABLE bool addNote(const QString& minute, qint64 playerId, const QString& text);
    Q_INVOKABLE bool updateNote(qint64 noteId, const QString& minute, qint64 playerId,
                                const QString& text);
    Q_INVOKABLE bool deleteNote(qint64 noteId);
    Q_INVOKABLE bool deleteTacticChange(qint64 changeId);
    // Brings back what was deleted last, from the undo toast.
    Q_INVOKABLE void undoDelete();

    // enterDigit() with the time given, for the tests.
    void enterDigitAt(int row, int column, int digit, qint64 elapsedMs);

signals:
    void matchChanged();
    void textsChanged();
    void noteChanged();
    void dirtyChanged();
    void loadErrorChanged();
    void saved();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);
    // Something was deleted; the view shows `text` in a toast with Undo (undoDelete()).
    void deleted(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Row {
        core::PlayerId player;
        std::optional<int> rating;
        std::optional<int> rpe;
        std::optional<int> savedRating;
        std::optional<int> savedRpe;
    };
    struct RememberedCell {
        int row = -1;
        int column = -1;
        std::optional<int> value;
    };
    struct LastOne {
        int row = -1;
        int column = -1;
        qint64 elapsedMs = 0;
    };
    using Deleted =
        std::variant<std::vector<core::MatchEvent>, core::MatchNote, core::MatchTacticChange>;

    void load(qint64 matchId, bool keepChanges);
    void rebuild();
    void rebuildRatings();
    void updateDirty();
    void setCell(int row, int column, std::optional<int> value);
    [[nodiscard]] bool isEditable(int row, int column) const;
    [[nodiscard]] QString rowError(const Row& row) const;
    [[nodiscard]] QString playerName(std::optional<core::PlayerId> player) const;
    [[nodiscard]] QString playerOption(core::PlayerId player) const;
    [[nodiscard]] QString eventText(const core::MatchEvent& event) const;
    [[nodiscard]] std::optional<core::MatchTime> parseMinute(const QString& minute,
                                                             QString& error) const;
    [[nodiscard]] std::optional<core::MatchEventDraft>
    draftOf(const QString& type, const QString& minute, qint64 player, qint64 related) const;
    [[nodiscard]] QString conflictText(const services::EventConflict& conflict) const;
    // Without EditLineup: errorOccurred, and the service is not called. The service checks the
    // same again (AGENTS section 4).
    [[nodiscard]] bool checkAllowed();
    // After a correction: read again, keeping the unsaved ratings.
    void afterChange();
    void report(const core::Error& error);

    services::MatchReportService& m_service;
    services::Session& m_session;
    MatchRatingsModel* m_ratings;
    MatchTimelineModel* m_timeline;
    MatchShapesModel* m_shapes;

    std::optional<services::MatchReport> m_report;
    std::vector<Row> m_rows;
    QString m_note;
    QString m_savedNote;
    bool m_dirty = false;
    std::optional<RememberedCell> m_remembered;
    std::optional<LastOne> m_lastOne;
    std::optional<Deleted> m_deleted;
    std::optional<core::Error> m_loadFailure;
    QElapsedTimer m_digitTimer;
};

} // namespace fdss::viewmodels
