#pragma once

#include "core/algorithms/LineupValidator.hpp"
#include "core/algorithms/Load.hpp"
#include "core/domain/Lineup.hpp"
#include "core/util/Error.hpp"
#include "services/LineupService.hpp"
#include "viewmodels/LineupModels.hpp"
#include "viewmodels/RowsModel.hpp"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <map>
#include <optional>
#include <vector>

namespace fdss::services {
class Clock;
class LoadService;
class Session;
struct TeamLoad;
} // namespace fdss::services

namespace fdss::viewmodels {

// The Pre-match screen (SPEC section 4 and 7.2, packet P12): the match, the tactics board, the
// bench, the squad to pick from and the issues of the lineup validator, checked again after every
// change (P12 decision 4). Saving, templates and importing need EditLineup and a planned match; the
// board is read-only otherwise. Load statuses (P11) come from the thread pool: until they are in,
// loadPending is true and the high-load rule has nothing to say.
class PreMatchViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no team. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    // The view binds it to AppSettings.yellowCardThreshold.
    Q_PROPERTY(int yellowCardThreshold READ yellowCardThreshold WRITE setYellowCardThreshold NOTIFY
                   yellowCardThresholdChanged FINAL)

    // The match
    Q_PROPERTY(fdss::viewmodels::ChoiceListModel* matches READ matches CONSTANT FINAL)
    // 0: no match is open.
    Q_PROPERTY(qint64 matchId READ matchId NOTIFY matchChanged FINAL)
    Q_PROPERTY(bool hasMatch READ hasMatch NOTIFY matchChanged FINAL)
    Q_PROPERTY(bool hasPlannedMatches READ hasPlannedMatches NOTIFY matchesChanged FINAL)
    Q_PROPERTY(QString opponentName READ opponentName NOTIFY matchChanged FINAL)
    // yyyy-MM-dd; empty without a match.
    Q_PROPERTY(QString matchDate READ matchDate NOTIFY matchChanged FINAL)
    Q_PROPERTY(QString matchDetailsText READ matchDetailsText NOTIFY textsChanged FINAL)
    Q_PROPERTY(QString lastMeetingText READ lastMeetingText NOTIFY textsChanged FINAL)
    Q_PROPERTY(int playerCount READ playerCount NOTIFY matchChanged FINAL)

    // The board
    // The catalogue name of the shape, or "custom".
    Q_PROPERTY(QString formation READ formation WRITE setFormation NOTIFY lineupChanged FINAL)
    // "4-4-2", or "4-3-1-2 (custom)".
    Q_PROPERTY(QString formationText READ formationText NOTIFY lineupChanged FINAL)
    // [{ value, text }]: the catalogue, and the custom shape first while there is one.
    Q_PROPERTY(QVariantList formationOptions READ formationOptions NOTIFY lineupChanged FINAL)
    // [{ value, text }] of the 14 positions, for the position of the selected slot.
    Q_PROPERTY(QVariantList positionOptions READ positionOptions NOTIFY textsChanged FINAL)
    // [{ position, text, x, y, width, height }]: the zones of the pitch, 0–1 (P12 decision 12).
    Q_PROPERTY(QVariantList zones READ zones NOTIFY textsChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::TacticsBoardModel* board READ board CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::BenchListModel* bench READ bench CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::LineupPlayersModel* players READ players CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::LineupIssuesModel* issues READ issues CONSTANT FINAL)
    Q_PROPERTY(int startingCount READ startingCount NOTIFY lineupChanged FINAL)
    Q_PROPERTY(int benchCount READ benchCount NOTIFY lineupChanged FINAL)
    Q_PROPERTY(int maxBench READ maxBench CONSTANT FINAL)
    // The keyboard path: -1 when no slot is selected.
    Q_PROPERTY(int selectedSlot READ selectedSlot NOTIFY selectionChanged FINAL)
    // The database text of the position of the selected slot; empty without one.
    Q_PROPERTY(QString selectedPosition READ selectedPosition NOTIFY selectionChanged FINAL)
    // The player in the selected slot; 0: none.
    Q_PROPERTY(qint64 selectedPlayerId READ selectedPlayerId NOTIFY selectionChanged FINAL)

    // The validator
    Q_PROPERTY(int errorCount READ errorCount NOTIFY issuesChanged FINAL)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY issuesChanged FINAL)
    // "Errors: 2 · Warnings: 1", or that the lineup has no issues.
    Q_PROPERTY(QString issuesSummary READ issuesSummary NOTIFY issuesChanged FINAL)
    Q_PROPERTY(bool loadPending READ loadPending NOTIFY loadPendingChanged FINAL)
    // The load statuses could not be read: the check is incomplete, the summary never says the
    // lineup is ready, and reload() tries again (P12 review).
    Q_PROPERTY(bool loadCheckFailed READ loadCheckFailed NOTIFY loadPendingChanged FINAL)
    // What to show for it; empty when the check is complete.
    Q_PROPERTY(QString loadCheckText READ loadCheckText NOTIFY textsChanged FINAL)

    // State
    // No EditLineup, or a match that is not planned, or no match.
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY matchChanged FINAL)
    // Why the board is read-only; empty when it is not.
    Q_PROPERTY(QString readOnlyText READ readOnlyText NOTIFY textsChanged FINAL)
    // The user changed the lineup since it was read or saved, so leaving must ask.
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    // A lineup is saved for the match.
    Q_PROPERTY(bool hasSavedLineup READ hasSavedLineup NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::ChoiceListModel* templates READ templates CONSTANT FINAL)
    Q_PROPERTY(bool hasTemplates READ hasTemplates NOTIFY templatesChanged FINAL)
    // Why the match could not be read; the view offers to try again. Empty: it is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    explicit PreMatchViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] int yellowCardThreshold() const;
    void setYellowCardThreshold(int threshold);

    [[nodiscard]] ChoiceListModel* matches() const;
    [[nodiscard]] qint64 matchId() const;
    [[nodiscard]] bool hasMatch() const;
    [[nodiscard]] bool hasPlannedMatches() const;
    [[nodiscard]] QString opponentName() const;
    [[nodiscard]] QString matchDate() const;
    [[nodiscard]] QString matchDetailsText() const;
    [[nodiscard]] QString lastMeetingText() const;
    [[nodiscard]] int playerCount() const;

    [[nodiscard]] QString formation() const;
    // A catalogue name changes the formation (changeFormation); anything else is ignored.
    void setFormation(const QString& formation);
    [[nodiscard]] QString formationText() const;
    [[nodiscard]] QVariantList formationOptions() const;
    [[nodiscard]] QVariantList positionOptions() const;
    [[nodiscard]] QVariantList zones() const;
    [[nodiscard]] TacticsBoardModel* board() const;
    [[nodiscard]] BenchListModel* bench() const;
    [[nodiscard]] LineupPlayersModel* players() const;
    [[nodiscard]] LineupIssuesModel* issues() const;
    [[nodiscard]] int startingCount() const;
    [[nodiscard]] int benchCount() const;
    [[nodiscard]] int maxBench() const;
    [[nodiscard]] int selectedSlot() const;
    [[nodiscard]] QString selectedPosition() const;
    [[nodiscard]] qint64 selectedPlayerId() const;

    [[nodiscard]] int errorCount() const;
    [[nodiscard]] int warningCount() const;
    [[nodiscard]] QString issuesSummary() const;
    [[nodiscard]] bool loadPending() const;
    [[nodiscard]] bool loadCheckFailed() const;
    [[nodiscard]] QString loadCheckText() const;

    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] QString readOnlyText() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool hasSavedLineup() const;
    [[nodiscard]] ChoiceListModel* templates() const;
    [[nodiscard]] bool hasTemplates() const;
    [[nodiscard]] QString loadError() const;

    // Reads the matches, the facts and the load statuses again. A lineup with changes of the user
    // stays as it is, without players who left the squad meanwhile. Facts that cannot be read set
    // loadError and leave the lineup on the screen as it is, dirty or not (P12 review).
    Q_INVOKABLE void reload();
    // Drops changes that were not saved; the view asks first. A match that cannot be read:
    // errorOccurred, and the one on the screen stays.
    Q_INVOKABLE void openMatch(qint64 matchId);
    // LineupService::nextPlannedMatch(); without one, no match is open.
    Q_INVOKABLE void openDefaultMatch();

    // -1 clears the selection.
    Q_INVOKABLE void selectSlot(int slot);
    // Drag and drop of a player on a slot: the player who was there takes the place the new one
    // came from (core::assignToSlot).
    Q_INVOKABLE bool assignToSlot(int slot, qint64 playerId);
    // The keyboard path: into the selected slot, or the first free one when none is selected;
    // then the selection moves on to the next free slot (none: no selection).
    Q_INVOKABLE bool pickPlayer(qint64 playerId);
    // A full bench: errorOccurred.
    Q_INVOKABLE bool addToBench(qint64 playerId);
    Q_INVOKABLE void swapSlots(int first, int second);
    Q_INVOKABLE bool moveSlotToBench(int slot);
    Q_INVOKABLE void removePlayer(qint64 playerId);
    // Clearing, loading a template and importing a file replace the lineup and emit lineupReplaced;
    // undoReplace() brings back the lineup from before, and with it the dirty state (P12 review).
    Q_INVOKABLE void clearLineup();
    Q_INVOKABLE void undoReplace();
    // A token dropped on the pitch (not on another token): the slot takes the position of the zone
    // under the point, x and y 0–1. A second goalkeeper: errorOccurred.
    Q_INVOKABLE bool moveSlotTo(int slot, double x, double y);
    // The keyboard path of the same: the position as its database text.
    Q_INVOKABLE bool setSlotPosition(int slot, const QString& position);
    // Selects the place of the issue and asks the view to focus it.
    Q_INVOKABLE void activateIssue(int row);

    // True when saved; also with errors, as a draft (the message says so).
    Q_INVOKABLE bool save();
    Q_INVOKABLE void discardChanges();

    // A name already taken and !replace: templateNameTaken, false.
    Q_INVOKABLE bool saveTemplate(const QString& name, bool replace);
    Q_INVOKABLE bool loadTemplate(qint64 templateId);
    // templateDeleted with the undo text.
    Q_INVOKABLE bool deleteTemplate(qint64 templateId);
    Q_INVOKABLE void undoDeleteTemplate();

    // A local file.
    Q_INVOKABLE bool exportTactic(const QUrl& file);
    Q_INVOKABLE bool importTactic(const QUrl& file);

signals:
    void teamIdChanged();
    void yellowCardThresholdChanged();
    void matchChanged();
    void matchesChanged();
    void lineupChanged();
    void selectionChanged();
    void issuesChanged();
    void loadPendingChanged();
    void dirtyChanged();
    void templatesChanged();
    void loadErrorChanged();
    // Texts that follow the language.
    void textsChanged();
    // Translated texts for the user; the view shows them in a toast.
    void saved(const QString& message);
    void notice(const QString& message);
    void errorOccurred(const QString& message);
    // The lineup was replaced as a whole; the view shows an undo toast (undoReplace()).
    void lineupReplaced(const QString& message);
    // The view asks whether to replace that template.
    void templateNameTaken(const QString& name);
    // The view shows an undo toast.
    void templateDeleted(const QString& message);
    void slotFocusRequested(int slot);
    void benchFocusRequested(int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Reads the planned matches; keeps the open match in the list even when it is not planned.
    void refreshMatches();
    void refreshTemplates();
    // Reads the facts of the match. False with `failure` set when it cannot be read.
    [[nodiscard]] bool readMatch(qint64 matchId, core::Error& failure);
    void requestLoad();
    void applyLoad(const services::TeamLoad& load);
    void clearMatch(std::optional<core::Error> failure);
    // After every change of m_lineup: validates, rebuilds the rows, updates dirty.
    void lineupEdited();
    void validate();
    void rebuildRows();
    void updateDirty();
    void setSelectedSlot(int slot);
    void setLoadPending(bool pending);
    void setLoadCheckFailed(bool failed);
    void rememberBeforeReplace();
    // The tooltip of a row of the squad: availability on the match day, load as of its day.
    [[nodiscard]] QString playerDetails(const services::LineupCandidate& item) const;
    // Without EditLineup or on a read-only board: errorOccurred, and nothing changes. The service
    // checks the permission again (AGENTS section 4).
    [[nodiscard]] bool checkEditable();
    // An edit that did not happen says why.
    [[nodiscard]] bool reportEdit(core::LineupEdit edit);
    void report(const core::Error& error);
    // Toasts about players left out of a lineup that was read, loaded or imported.
    void reportDropped(int dropped);
    [[nodiscard]] QString playerName(core::PlayerId player) const;
    [[nodiscard]] const services::LineupCandidate* candidate(core::PlayerId player) const;
    [[nodiscard]] core::LineupContext contextWithLoad() const;

    services::LineupService& m_lineupService;
    services::LoadService& m_load;
    services::Session& m_session;
    const services::Clock& m_clock;
    ChoiceListModel* m_matches;
    TacticsBoardModel* m_board;
    BenchListModel* m_bench;
    LineupPlayersModel* m_players;
    LineupIssuesModel* m_issues;
    ChoiceListModel* m_templates;

    qint64 m_teamId = 0;
    int m_yellowCardThreshold = core::kDefaultYellowCardThreshold;
    bool m_hasPlannedMatches = false;
    std::optional<services::MatchLineup> m_data;
    core::Lineup m_lineup;
    core::Lineup m_baseline;
    std::vector<core::LineupIssue> m_issueList;
    std::map<core::PlayerId, core::LoadStatus> m_loadStatuses;
    std::map<core::PlayerId, core::Date> m_loadAsOf;
    bool m_loadPending = false;
    bool m_loadCheckFailed = false;
    // The lineup before the last clear, template or import, for undoReplace().
    std::optional<core::Lineup> m_beforeReplace;
    // Every load request gets the next number; only the newest result counts.
    int m_loadRequest = 0;
    int m_selectedSlot = -1;
    bool m_dirty = false;
    std::optional<services::DeletedTemplate> m_deletedTemplate;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
