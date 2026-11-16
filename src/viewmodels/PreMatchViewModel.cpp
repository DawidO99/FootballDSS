#include "viewmodels/PreMatchViewModel.hpp"

#include "core/algorithms/Formations.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Permissions.hpp"
#include "services/Clock.hpp"
#include "services/LoadService.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/LineupTexts.hpp"
#include "viewmodels/LoadTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFuture>
#include <QStringList>

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace fdss::viewmodels {

namespace {

const auto kCustom = QStringLiteral("custom");
const auto kXi = QStringLiteral("xi");
const auto kBench = QStringLiteral("bench");

QVariantMap option(const QString& value, const QString& text) {
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}};
}

QVariant shirtNumberOf(const core::Player& player) {
    return player.shirtNumber ? QVariant(*player.shirtNumber) : QVariant{};
}

// "CM (DM, AM)".
QString positionsText(const core::Player& player) {
    QString text = SquadTexts::positionCode(player.primaryPosition);
    if (!player.secondaryPositions.empty()) {
        QStringList codes;
        for (const core::Position position : player.secondaryPositions) {
            codes.append(SquadTexts::positionCode(position));
        }
        text += QStringLiteral(" (%1)").arg(codes.join(QStringLiteral(", ")));
    }
    return text;
}

// The worse of two severities; empty is none.
QString worse(const QString& a, const QString& b) {
    if (a == QLatin1String("error") || b == QLatin1String("error")) {
        return QStringLiteral("error");
    }
    return a.isEmpty() ? b : a;
}

QVariant idOrZero(const std::optional<core::PlayerId>& player) {
    return QVariant(static_cast<qint64>(player ? player->value : 0));
}

// The order of the load column: the statuses to talk about first; not enough data has no value.
QVariant loadRank(core::LoadStatus status) {
    switch (status) {
    case core::LoadStatus::High:
        return 0;
    case core::LoadStatus::Elevated:
        return 1;
    case core::LoadStatus::Normal:
        return 2;
    case core::LoadStatus::Underloaded:
        return 3;
    case core::LoadStatus::NotEnoughData:
        return {};
    }
    return {};
}

} // namespace

PreMatchViewModel::PreMatchViewModel(QObject* parent)
    : QObject(parent), m_lineupService(ServiceRegistry::instance().lineup()),
      m_load(ServiceRegistry::instance().load()), m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()), m_matches(new ChoiceListModel(this)),
      m_board(new TacticsBoardModel(this)), m_bench(new BenchListModel(this)),
      m_players(new LineupPlayersModel(this)),
      m_sortedPlayers(new LineupPlayersSortModel(m_players, this)),
      m_picker(new SlotPickerModel(this)), m_issues(new LineupIssuesModel(this)),
      m_templates(new ChoiceListModel(this)) {
    // Once per change of the language, see LoadViewModel.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 PreMatchViewModel::teamId() const { return m_teamId; }

void PreMatchViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    refreshTemplates();
    openDefaultMatch();
}

int PreMatchViewModel::yellowCardThreshold() const { return m_yellowCardThreshold; }

void PreMatchViewModel::setYellowCardThreshold(int threshold) {
    if (threshold == m_yellowCardThreshold) {
        return;
    }
    m_yellowCardThreshold = threshold;
    emit yellowCardThresholdChanged();
    validate();
    rebuildRows();
    emit issuesChanged();
}

ChoiceListModel* PreMatchViewModel::matches() const { return m_matches; }

qint64 PreMatchViewModel::matchId() const { return m_data ? m_data->match.match.id.value : 0; }

bool PreMatchViewModel::hasMatch() const { return m_data.has_value(); }

bool PreMatchViewModel::hasPlannedMatches() const { return m_hasPlannedMatches; }

QString PreMatchViewModel::opponentName() const {
    return m_data ? QString::fromStdString(m_data->match.opponentName) : QString{};
}

QString PreMatchViewModel::matchDate() const {
    return m_data ? IsoDates::toIso(m_data->match.match.date) : QString{};
}

QString PreMatchViewModel::matchDetailsText() const {
    return m_data ? LineupTexts::matchDetails(m_data->match.match) : QString{};
}

QString PreMatchViewModel::lastMeetingText() const {
    return m_data ? LineupTexts::lastMeeting(m_data->lastMeeting) : QString{};
}

int PreMatchViewModel::playerCount() const {
    return m_data ? static_cast<int>(m_data->candidates.size()) : 0;
}

QString PreMatchViewModel::formation() const {
    if (!m_data) {
        return {};
    }
    const core::Formation* formation = core::matchingFormation(m_lineup.pitchSlots);
    return formation ? QString::fromUtf8(formation->name) : kCustom;
}

void PreMatchViewModel::setFormation(const QString& formation) {
    const core::Formation* target = core::findFormation(formation.toStdString());
    if (!target || target == core::matchingFormation(m_lineup.pitchSlots) || !checkEditable()) {
        return;
    }
    core::FormationChange change = core::changeFormation(m_lineup, *target);
    m_lineup = std::move(change.lineup);
    QStringList moved;
    for (const core::PlayerId player : change.movedToBench) {
        moved.append(playerName(player));
    }
    if (!moved.isEmpty()) {
        emit notice(tr("Moved to the bench: %1.").arg(moved.join(QStringLiteral(", "))));
    }
    QStringList left;
    for (const core::PlayerId player : change.leftLineup) {
        left.append(playerName(player));
    }
    if (!left.isEmpty()) {
        emit notice(tr("The bench is full. Left out of the lineup: %1.")
                        .arg(left.join(QStringLiteral(", "))));
    }
    setSelectedSlot(-1);
    lineupEdited();
}

QString PreMatchViewModel::formationText() const {
    return m_data ? LineupTexts::formationText(m_lineup) : QString{};
}

QVariantList PreMatchViewModel::formationOptions() const {
    QVariantList options;
    if (m_data && !core::matchingFormation(m_lineup.pitchSlots)) {
        options.append(option(kCustom, LineupTexts::formationText(m_lineup)));
    }
    for (const core::Formation& formation : core::formations()) {
        const QString name = QString::fromUtf8(formation.name);
        options.append(option(name, name));
    }
    return options;
}

QVariantList PreMatchViewModel::positionOptions() const { return SquadTexts::positionOptions(); }

QVariantList PreMatchViewModel::zones() const {
    QVariantList zones;
    for (const core::PitchZone& zone : core::pitchZones()) {
        zones.append(QVariantMap{
            {QStringLiteral("position"), QString::fromUtf8(core::toDbText(zone.position))},
            {QStringLiteral("text"), SquadTexts::positionCode(zone.position)},
            {QStringLiteral("x"), zone.left},
            {QStringLiteral("y"), zone.top},
            {QStringLiteral("width"), zone.width},
            {QStringLiteral("height"), zone.height},
        });
    }
    return zones;
}

TacticsBoardModel* PreMatchViewModel::board() const { return m_board; }

BenchListModel* PreMatchViewModel::bench() const { return m_bench; }

LineupPlayersModel* PreMatchViewModel::players() const { return m_players; }

LineupIssuesModel* PreMatchViewModel::issues() const { return m_issues; }

LineupPlayersSortModel* PreMatchViewModel::sortedPlayers() const { return m_sortedPlayers; }

SlotPickerModel* PreMatchViewModel::slotPicker() const { return m_picker; }

int PreMatchViewModel::pickerSlot() const { return m_pickerSlot; }

QString PreMatchViewModel::pickerTitle() const {
    if (!core::isValidSlot(m_lineup, m_pickerSlot)) {
        return {};
    }
    return tr("%1: choose a player")
        .arg(SquadTexts::positionName(
            m_lineup.pitchSlots[static_cast<std::size_t>(m_pickerSlot)].position));
}

int PreMatchViewModel::startingCount() const { return core::startingCount(m_lineup); }

int PreMatchViewModel::benchCount() const { return static_cast<int>(m_lineup.bench.size()); }

int PreMatchViewModel::maxBench() const { return core::kMaxBench; }

int PreMatchViewModel::selectedSlot() const { return m_selectedSlot; }

QString PreMatchViewModel::selectedPosition() const {
    if (!core::isValidSlot(m_lineup, m_selectedSlot)) {
        return {};
    }
    return QString::fromUtf8(
        core::toDbText(m_lineup.pitchSlots[static_cast<std::size_t>(m_selectedSlot)].position));
}

qint64 PreMatchViewModel::selectedPlayerId() const {
    if (!core::isValidSlot(m_lineup, m_selectedSlot)) {
        return 0;
    }
    const auto& player = m_lineup.pitchSlots[static_cast<std::size_t>(m_selectedSlot)].player;
    return player ? player->value : 0;
}

int PreMatchViewModel::errorCount() const {
    return static_cast<int>(
        std::ranges::count(m_issueList, core::LineupSeverity::Error, &core::LineupIssue::severity));
}

int PreMatchViewModel::warningCount() const {
    return static_cast<int>(std::ranges::count(m_issueList, core::LineupSeverity::Warning,
                                               &core::LineupIssue::severity));
}

QString PreMatchViewModel::issuesSummary() const {
    if (!m_data) {
        return {};
    }
    // Never "ready" while the load is not checked (P12 review).
    if (m_issueList.empty() && !m_loadPending && !m_loadCheckFailed) {
        return tr("No issues: the lineup is ready for Match Day.");
    }
    return tr("Errors: %1 · Warnings: %2").arg(errorCount()).arg(warningCount());
}

bool PreMatchViewModel::loadCheckFailed() const { return m_loadCheckFailed; }

QString PreMatchViewModel::loadCheckText() const {
    return m_loadCheckFailed
               ? tr("The check is incomplete: the load could not be checked. Try again.")
               : QString{};
}

bool PreMatchViewModel::loadPending() const { return m_loadPending; }

bool PreMatchViewModel::readOnly() const {
    return !m_data || !m_session.isAllowed(core::Permission::EditLineup) ||
           m_data->match.match.status != core::MatchStatus::Planned;
}

QString PreMatchViewModel::readOnlyText() const {
    if (!m_data) {
        return {};
    }
    if (!m_session.isAllowed(core::Permission::EditLineup)) {
        return tr("Only the coach or the assistant can change the lineup.");
    }
    if (m_data->match.match.status != core::MatchStatus::Planned) {
        return tr("This match has already started or finished, so its lineup is shown read-only.");
    }
    return {};
}

bool PreMatchViewModel::dirty() const { return m_dirty; }

bool PreMatchViewModel::hasSavedLineup() const { return m_data && m_data->saved; }

ChoiceListModel* PreMatchViewModel::templates() const { return m_templates; }

bool PreMatchViewModel::hasTemplates() const { return m_templates->rowCount() > 0; }

QString PreMatchViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void PreMatchViewModel::reload() {
    refreshTemplates();
    if (!m_data) {
        openDefaultMatch();
        return;
    }
    auto result = m_lineupService.matchLineup(m_data->match.match.id, m_yellowCardThreshold);
    if (!result) {
        // Only the facts could not be read again: the lineup on the screen, changed or not, stays
        // with its dirty state, so leaving still asks (P12 review).
        qCritical("PreMatchViewModel: %s", result.error().detail.c_str());
        m_loadFailure = result.error();
        emit loadErrorChanged();
        return;
    }
    m_loadFailure.reset();
    emit loadErrorChanged();
    const bool keepChanges = m_dirty;
    core::Lineup edited = m_lineup;
    m_data = std::move(*result);
    if (keepChanges) {
        std::set<core::PlayerId> squad;
        for (const auto& item : m_data->candidates) {
            squad.insert(item.player.id);
        }
        reportDropped(services::dropOutsiders(edited, squad));
        m_lineup = std::move(edited);
    } else {
        m_lineup = m_data->lineup;
        reportDropped(m_data->droppedPlayers);
    }
    m_baseline = m_data->lineup;
    refreshMatches();
    emit matchChanged();
    emit textsChanged();
    requestLoad();
    lineupEdited();
}

void PreMatchViewModel::openMatch(qint64 matchId) {
    core::Error failure;
    if (readMatch(matchId, failure)) {
        return;
    }
    qCritical("PreMatchViewModel: %s", failure.detail.c_str());
    if (m_data) {
        report(failure);
        refreshMatches();
        return;
    }
    clearMatch(failure);
}

void PreMatchViewModel::openDefaultMatch() {
    if (m_teamId == 0 || !m_session.user()) {
        clearMatch(std::nullopt);
        return;
    }
    const auto next = m_lineupService.nextPlannedMatch(core::TeamId{m_teamId});
    if (!next) {
        qCritical("PreMatchViewModel: %s", next.error().detail.c_str());
        clearMatch(next.error());
        return;
    }
    if (!*next) {
        clearMatch(std::nullopt);
        return;
    }
    openMatch((*next)->value);
}

void PreMatchViewModel::selectSlot(int slot) {
    setSelectedSlot(core::isValidSlot(m_lineup, slot) ? slot : -1);
    rebuildRows();
}

bool PreMatchViewModel::assignToSlot(int slot, qint64 playerId) {
    if (!checkEditable() || !candidate(core::PlayerId{playerId})) {
        return false;
    }
    if (!reportEdit(core::assignToSlot(m_lineup, slot, core::PlayerId{playerId}))) {
        return false;
    }
    lineupEdited();
    return true;
}

bool PreMatchViewModel::pickPlayer(qint64 playerId) {
    if (!checkEditable() || !candidate(core::PlayerId{playerId})) {
        return false;
    }
    int target = m_selectedSlot;
    if (!core::isValidSlot(m_lineup, target)) {
        const auto free = std::ranges::find_if(
            m_lineup.pitchSlots, [](const core::LineupSlot& slot) { return !slot.player; });
        if (free == m_lineup.pitchSlots.end()) {
            emit errorOccurred(
                tr("Every slot is taken. Select a slot first to replace its player."));
            return false;
        }
        target = static_cast<int>(free - m_lineup.pitchSlots.begin());
    }
    const core::LineupEdit edit = core::assignToSlot(m_lineup, target, core::PlayerId{playerId});
    if (edit != core::LineupEdit::Done && edit != core::LineupEdit::Unchanged) {
        return reportEdit(edit);
    }
    // On to the next free slot after this one, round the board.
    int next = -1;
    const int count = static_cast<int>(m_lineup.pitchSlots.size());
    for (int step = 1; step <= count; ++step) {
        const int slot = (target + step) % count;
        if (!m_lineup.pitchSlots[static_cast<std::size_t>(slot)].player) {
            next = slot;
            break;
        }
    }
    // The focus stays in the list, so the next Enter fills the next slot.
    setSelectedSlot(next);
    lineupEdited();
    return true;
}

bool PreMatchViewModel::addToBench(qint64 playerId) {
    if (!checkEditable() || !candidate(core::PlayerId{playerId})) {
        return false;
    }
    if (!reportEdit(core::addToBench(m_lineup, core::PlayerId{playerId}))) {
        return false;
    }
    lineupEdited();
    return true;
}

void PreMatchViewModel::swapSlots(int first, int second) {
    if (!checkEditable() || !reportEdit(core::swapSlots(m_lineup, first, second))) {
        return;
    }
    lineupEdited();
}

bool PreMatchViewModel::moveSlotToBench(int slot) {
    if (!checkEditable() || !reportEdit(core::moveSlotToBench(m_lineup, slot))) {
        return false;
    }
    lineupEdited();
    return true;
}

void PreMatchViewModel::removePlayer(qint64 playerId) {
    if (!checkEditable() || !reportEdit(core::removePlayer(m_lineup, core::PlayerId{playerId}))) {
        return;
    }
    lineupEdited();
}

void PreMatchViewModel::clearLineup() {
    if (!checkEditable()) {
        return;
    }
    rememberBeforeReplace();
    core::clearPlayers(m_lineup);
    setSelectedSlot(-1);
    lineupEdited();
    emit lineupReplaced(tr("The lineup was cleared."));
}

void PreMatchViewModel::undoReplace() {
    if (!m_beforeReplace || !m_data) {
        return;
    }
    m_lineup = std::move(*m_beforeReplace);
    m_beforeReplace.reset();
    setSelectedSlot(-1);
    lineupEdited();
}

bool PreMatchViewModel::moveSlotTo(int slot, double x, double y) {
    if (!checkEditable() ||
        !reportEdit(core::moveSlot(m_lineup, slot, core::positionAt(x, y), x))) {
        return false;
    }
    lineupEdited();
    return true;
}

bool PreMatchViewModel::setSlotPosition(int slot, const QString& position) {
    const auto parsed = core::fromDbText<core::Position>(position.toStdString());
    if (!parsed) {
        qWarning("PreMatchViewModel: unknown position '%s'", qUtf8Printable(position));
        return false;
    }
    if (!checkEditable() || !reportEdit(core::moveSlot(m_lineup, slot, *parsed))) {
        return false;
    }
    lineupEdited();
    return true;
}

bool PreMatchViewModel::openPicker(int slot) {
    if (!core::isValidSlot(m_lineup, slot) || !checkEditable()) {
        return false;
    }
    setSelectedSlot(slot);
    setPickerSlot(slot);
    rebuildRows();
    return true;
}

bool PreMatchViewModel::pickForSlot(qint64 playerId) {
    if (!core::isValidSlot(m_lineup, m_pickerSlot)) {
        return false;
    }
    const auto& current = m_lineup.pitchSlots[static_cast<std::size_t>(m_pickerSlot)].player;
    if (current && current->value == playerId) {
        return true;
    }
    return assignToSlot(m_pickerSlot, playerId);
}

void PreMatchViewModel::closePicker() {
    setPickerSlot(-1);
    m_picker->setRows({});
}

void PreMatchViewModel::activateIssue(int row) {
    if (row < 0 || row >= std::ssize(m_issueList)) {
        return;
    }
    const core::LineupIssue& issue = m_issueList[static_cast<std::size_t>(row)];
    if (issue.benchIndex) {
        emit benchFocusRequested(*issue.benchIndex);
        return;
    }
    int slot = issue.slot.value_or(-1);
    if (slot < 0) {
        // The whole lineup: the goalkeeper slot, or the first free one.
        const auto found = std::ranges::find_if(m_lineup.pitchSlots, [&](const auto& candidate) {
            return issue.rule == core::LineupRule::Goalkeeper
                       ? candidate.position == core::Position::Gk
                       : !candidate.player.has_value();
        });
        slot = found == m_lineup.pitchSlots.end()
                   ? 0
                   : static_cast<int>(found - m_lineup.pitchSlots.begin());
    }
    selectSlot(slot);
    emit slotFocusRequested(slot);
}

bool PreMatchViewModel::save() {
    if (!checkEditable()) {
        return false;
    }
    if (auto saved = m_lineupService.save(m_data->match.match.id, m_lineup); !saved) {
        report(saved.error());
        return false;
    }
    m_lineup.formation = core::formationName(m_lineup.pitchSlots);
    m_baseline = m_lineup;
    m_data->lineup = m_lineup;
    m_data->saved = true;
    updateDirty();
    emit dirtyChanged();
    const int errors = errorCount();
    emit saved(errors > 0
                   ? tr("Lineup saved as a draft. Errors to fix before Match Day: %1.").arg(errors)
                   : tr("Lineup saved."));
    return true;
}

void PreMatchViewModel::discardChanges() {
    if (!m_data) {
        return;
    }
    m_lineup = m_baseline;
    setSelectedSlot(-1);
    lineupEdited();
}

bool PreMatchViewModel::saveTemplate(const QString& name, bool replace) {
    if (!m_data) {
        return false;
    }
    if (!m_session.isAllowed(core::Permission::EditLineup)) {
        emit errorOccurred(readOnlyText());
        return false;
    }
    const auto saved = m_lineupService.saveTemplate(name.toStdString(), m_lineup, replace);
    if (!saved) {
        if (!replace && saved.error().code == core::ErrorCode::Conflict &&
            saved.error().field == "template_name") {
            emit templateNameTaken(name.trimmed());
            return false;
        }
        report(saved.error());
        return false;
    }
    refreshTemplates();
    emit notice(tr("Template saved: %1.").arg(name.trimmed()));
    return true;
}

bool PreMatchViewModel::loadTemplate(qint64 templateId) {
    if (!checkEditable()) {
        return false;
    }
    auto loaded = m_lineupService.loadTemplate(core::LineupId{templateId}, core::TeamId{m_teamId});
    if (!loaded) {
        report(loaded.error());
        return false;
    }
    rememberBeforeReplace();
    m_lineup = std::move(loaded->lineup);
    reportDropped(loaded->droppedPlayers);
    setSelectedSlot(-1);
    lineupEdited();
    emit lineupReplaced(tr("The template was loaded into the board."));
    return true;
}

bool PreMatchViewModel::deleteTemplate(qint64 templateId) {
    if (!m_session.isAllowed(core::Permission::EditLineup)) {
        emit errorOccurred(tr("Only the coach or the assistant can change the lineup."));
        return false;
    }
    auto deleted = m_lineupService.deleteTemplate(core::LineupId{templateId});
    if (!deleted) {
        report(deleted.error());
        return false;
    }
    const QString name = QString::fromStdString(deleted->name);
    m_deletedTemplate = std::move(*deleted);
    refreshTemplates();
    emit templateDeleted(tr("Template deleted: %1.").arg(name));
    return true;
}

void PreMatchViewModel::undoDeleteTemplate() {
    if (!m_deletedTemplate) {
        return;
    }
    const auto restored = m_lineupService.restoreTemplate(*m_deletedTemplate);
    m_deletedTemplate.reset();
    if (!restored) {
        report(restored.error());
        return;
    }
    refreshTemplates();
}

bool PreMatchViewModel::exportTactic(const QUrl& file) {
    if (!m_data) {
        return false;
    }
    const QString path = file.toLocalFile();
    if (path.isEmpty()) {
        qWarning("PreMatchViewModel: not a local file: %s", qUtf8Printable(file.toString()));
        return false;
    }
    if (auto written = m_lineupService.exportTactic(path, m_lineup); !written) {
        report(written.error());
        return false;
    }
    emit notice(tr("Lineup exported: %1.").arg(QFileInfo(path).fileName()));
    return true;
}

bool PreMatchViewModel::importTactic(const QUrl& file) {
    if (!checkEditable()) {
        return false;
    }
    const QString path = file.toLocalFile();
    if (path.isEmpty()) {
        qWarning("PreMatchViewModel: not a local file: %s", qUtf8Printable(file.toString()));
        return false;
    }
    auto loaded = m_lineupService.importTactic(path, core::TeamId{m_teamId});
    if (!loaded) {
        report(loaded.error());
        return false;
    }
    rememberBeforeReplace();
    m_lineup = std::move(loaded->lineup);
    reportDropped(loaded->droppedPlayers);
    setSelectedSlot(-1);
    lineupEdited();
    emit lineupReplaced(tr("Lineup imported: %1.").arg(QFileInfo(path).fileName()));
    return true;
}

bool PreMatchViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        refreshMatches();
        refreshTemplates();
        rebuildRows();
        emit textsChanged();
        emit issuesChanged();
        emit lineupChanged();
        emit loadErrorChanged();
        emit pickerChanged();
    }
    return QObject::eventFilter(watched, event);
}

void PreMatchViewModel::refreshMatches() {
    std::vector<services::CalendarMatch> list;
    if (m_teamId != 0 && m_session.user()) {
        auto planned = m_lineupService.plannedMatches(core::TeamId{m_teamId});
        if (!planned) {
            qCritical("PreMatchViewModel: %s", planned.error().detail.c_str());
        } else {
            list = std::move(*planned);
        }
    }
    const bool hasPlanned = !list.empty();
    // The open match stays in the field even when it is not planned.
    if (m_data && std::ranges::none_of(list, [&](const services::CalendarMatch& item) {
            return item.match.id == m_data->match.match.id;
        })) {
        const auto place = std::ranges::find_if(list, [&](const services::CalendarMatch& item) {
            return item.match.date > m_data->match.match.date;
        });
        list.insert(place, m_data->match);
    }
    QList<QVariantList> rows;
    for (const services::CalendarMatch& item : list) {
        rows.append(QVariantList{QVariant(static_cast<qint64>(item.match.id.value)),
                                 LineupTexts::matchOption(item)});
    }
    m_matches->setRows(std::move(rows));
    m_hasPlannedMatches = hasPlanned;
    emit matchesChanged();
}

void PreMatchViewModel::refreshTemplates() {
    QList<QVariantList> rows;
    if (m_session.user()) {
        auto templates = m_lineupService.templates();
        if (!templates) {
            qCritical("PreMatchViewModel: %s", templates.error().detail.c_str());
        } else {
            std::ranges::sort(*templates, [](const services::LineupTemplateInfo& a,
                                             const services::LineupTemplateInfo& b) {
                return QString::fromStdString(a.name).localeAwareCompare(
                           QString::fromStdString(b.name)) < 0;
            });
            for (const services::LineupTemplateInfo& item : *templates) {
                rows.append(
                    QVariantList{QVariant(static_cast<qint64>(item.id.value)),
                                 tr("%1 · %2").arg(QString::fromStdString(item.name),
                                                   QString::fromStdString(item.formation))});
            }
        }
    }
    m_templates->setRows(std::move(rows));
    emit templatesChanged();
}

bool PreMatchViewModel::readMatch(qint64 matchId, core::Error& failure) {
    auto result = m_lineupService.matchLineup(core::MatchId{matchId}, m_yellowCardThreshold);
    if (!result) {
        failure = result.error();
        return false;
    }
    m_data = std::move(*result);
    m_lineup = m_data->lineup;
    m_baseline = m_lineup;
    m_beforeReplace.reset();
    m_loadFailure.reset();
    m_selectedSlot = -1;
    setPickerSlot(-1);
    refreshMatches();
    emit matchChanged();
    emit textsChanged();
    emit loadErrorChanged();
    emit selectionChanged();
    reportDropped(m_data->droppedPlayers);
    requestLoad();
    lineupEdited();
    return true;
}

void PreMatchViewModel::requestLoad() {
    const int request = ++m_loadRequest;
    m_loadStatuses.clear();
    m_loadAsOf.clear();
    if (!m_data || !m_session.isAllowed(core::Permission::ViewAnalysis)) {
        setLoadCheckFailed(false);
        setLoadPending(false);
        return;
    }
    auto future = m_load.teamLoad(m_data->match.match.teamId);
    if (!future) {
        // The lineup still works without load statuses, but the check is incomplete and says so
        // until the next reload succeeds (P12 review).
        qCritical("PreMatchViewModel: %s", future.error().detail.c_str());
        setLoadCheckFailed(true);
        setLoadPending(false);
        return;
    }
    setLoadCheckFailed(false);
    setLoadPending(true);
    future->then(this, [this, request](services::TeamLoad result) {
        if (request == m_loadRequest) {
            applyLoad(result);
        }
    });
}

void PreMatchViewModel::applyLoad(const services::TeamLoad& load) {
    m_loadStatuses.clear();
    m_loadAsOf.clear();
    for (const services::PlayerLoadRow& row : load.players) {
        m_loadStatuses.emplace(row.player.id, row.assessment.verdict.status);
        m_loadAsOf.emplace(row.player.id, row.assessment.asOf);
    }
    setLoadPending(false);
    validate();
    rebuildRows();
    emit issuesChanged();
}

void PreMatchViewModel::clearMatch(std::optional<core::Error> failure) {
    ++m_loadRequest;
    m_data.reset();
    m_lineup = core::Lineup{};
    m_baseline = core::Lineup{};
    m_beforeReplace.reset();
    m_loadStatuses.clear();
    m_loadAsOf.clear();
    setLoadCheckFailed(false);
    m_loadFailure = std::move(failure);
    m_selectedSlot = -1;
    setPickerSlot(-1);
    setLoadPending(false);
    refreshMatches();
    emit matchChanged();
    emit textsChanged();
    emit loadErrorChanged();
    emit selectionChanged();
    lineupEdited();
}

void PreMatchViewModel::lineupEdited() {
    validate();
    rebuildRows();
    updateDirty();
    emit lineupChanged();
    emit issuesChanged();
    emit selectionChanged();
}

void PreMatchViewModel::validate() {
    m_issueList = m_data ? m_lineupService.check(m_lineup, contextWithLoad())
                         : std::vector<core::LineupIssue>{};
}

void PreMatchViewModel::rebuildRows() {
    // Issues by place and player.
    std::map<int, std::pair<QString, QStringList>> bySlot;
    std::map<int, std::pair<QString, QStringList>> byBench;
    std::map<core::PlayerId, std::pair<QString, QStringList>> byPlayer;
    QList<QVariantList> issueRows;
    for (const core::LineupIssue& issue : m_issueList) {
        const QString severity = LineupTexts::severityKey(issue.severity);
        const QString text =
            LineupTexts::issueText(issue, issue.player ? playerName(*issue.player) : QString{});
        const auto add = [&](std::pair<QString, QStringList>& entry) {
            entry.first = worse(entry.first, severity);
            entry.second.append(text);
        };
        if (issue.slot) {
            add(bySlot[*issue.slot]);
        }
        if (issue.benchIndex) {
            add(byBench[*issue.benchIndex]);
        }
        if (issue.player) {
            add(byPlayer[*issue.player]);
        }
        issueRows.append(QVariantList{
            severity, LineupTexts::severityName(issue.severity), LineupTexts::ruleKey(issue.rule),
            text, idOrZero(issue.player), issue.slot.value_or(-1), issue.benchIndex.value_or(-1)});
    }
    m_issues->setRows(std::move(issueRows));

    const auto newline = QStringLiteral("\n");
    QList<QVariantList> boardRows;
    for (std::size_t i = 0; i < m_lineup.pitchSlots.size(); ++i) {
        const core::LineupSlot& slot = m_lineup.pitchSlots[i];
        const auto* player = slot.player ? candidate(*slot.player) : nullptr;
        const auto& issues = bySlot[static_cast<int>(i)];
        boardRows.append(
            QVariantList{static_cast<int>(i), QString::fromUtf8(core::toDbText(slot.position)),
                         SquadTexts::positionCode(slot.position), slot.x, slot.y,
                         idOrZero(slot.player), player ? shirtNumberOf(player->player) : QVariant{},
                         player ? QString::fromStdString(player->player.lastName) : QString{},
                         player ? SquadTexts::displayName(player->player) : QString{}, issues.first,
                         issues.second.join(newline), static_cast<int>(i) == m_selectedSlot});
    }
    m_board->setRows(std::move(boardRows));

    QList<QVariantList> benchRows;
    for (std::size_t i = 0; i < m_lineup.bench.size(); ++i) {
        const core::PlayerId id = m_lineup.bench[i];
        const auto* player = candidate(id);
        const auto& issues = byBench[static_cast<int>(i)];
        benchRows.append(
            QVariantList{static_cast<int>(i), QVariant(static_cast<qint64>(id.value)),
                         player ? shirtNumberOf(player->player) : QVariant{}, playerName(id),
                         player ? QString::fromStdString(player->player.lastName) : QString{},
                         player ? positionsText(player->player) : QString{}, issues.first,
                         issues.second.join(newline)});
    }
    m_bench->setRows(std::move(benchRows));

    rebuildPlayerRows(byPlayer);
    rebuildPicker();
}

void PreMatchViewModel::rebuildPlayerRows(
    std::map<core::PlayerId, std::pair<QString, QStringList>>& byPlayer) {
    // The XI from the goalkeeper up the pitch (own goal at y = 1), then from the left.
    std::vector<int> pitchOrder(m_lineup.pitchSlots.size());
    for (std::size_t i = 0; i < pitchOrder.size(); ++i) {
        pitchOrder[i] = static_cast<int>(i);
    }
    std::ranges::stable_sort(pitchOrder, [&](int a, int b) {
        const auto& left = m_lineup.pitchSlots[static_cast<std::size_t>(a)];
        const auto& right = m_lineup.pitchSlots[static_cast<std::size_t>(b)];
        return left.y != right.y ? left.y > right.y : left.x < right.x;
    });
    std::map<int, int> rankOfSlot;
    for (std::size_t rank = 0; rank < pitchOrder.size(); ++rank) {
        rankOfSlot[pitchOrder[rank]] = static_cast<int>(rank);
    }
    const std::array<QString, 3> sectionTexts{tr("Starting XI"), tr("Bench"),
                                              tr("Not in the lineup")};
    QList<QVariantList> playerRows;
    if (m_data) {
        for (const services::LineupCandidate& item : m_data->candidates) {
            const core::Player& player = item.player;
            const auto place = core::findPlayer(m_lineup, player.id);
            const bool inXi = place && place->kind == core::LineupPlace::Kind::Slot;
            const QString placement = !place ? QString{} : inXi ? kXi : kBench;
            const int section = !place ? 2 : inXi ? 0 : 1;
            const int sectionOrder = !place ? 0 : inXi ? rankOfSlot[place->index] : place->index;
            const auto load = m_loadStatuses.find(player.id);
            const auto& issues = byPlayer[player.id];
            playerRows.append(QVariantList{
                QVariant(static_cast<qint64>(player.id.value)),
                shirtNumberOf(player),
                SquadTexts::displayName(player),
                QString::fromStdString(player.lastName),
                positionsText(player),
                SquadTexts::positionCode(player.primaryPosition),
                SquadTexts::statusKey(item.facts.statusOnMatchDay, false),
                SquadTexts::statusName(item.facts.statusOnMatchDay, false),
                load == m_loadStatuses.end() ? QString{} : LoadTexts::statusKey(load->second),
                load == m_loadStatuses.end() ? QString{} : LoadTexts::statusShortName(load->second),
                placement,
                LineupTexts::placementName(placement),
                issues.first,
                issues.second.join(QLatin1Char('\n')),
                playerDetails(item),
                section,
                sectionTexts[static_cast<std::size_t>(section)],
                sectionOrder,
                QString::fromStdString(player.lastName),
                QString::fromStdString(player.firstName),
                static_cast<int>(player.primaryPosition),
                static_cast<int>(item.facts.statusOnMatchDay),
                load == m_loadStatuses.end() ? QVariant{} : loadRank(load->second)});
        }
    }
    m_players->setRows(std::move(playerRows));
}

void PreMatchViewModel::rebuildPicker() {
    if (!m_data || !core::isValidSlot(m_lineup, m_pickerSlot)) {
        m_picker->setRows({});
        return;
    }
    const core::LineupSlot& slot = m_lineup.pitchSlots[static_cast<std::size_t>(m_pickerSlot)];
    const QString code = SquadTexts::positionCode(slot.position);
    const std::array<QString, 3> groupKeys{QStringLiteral("primary"), QStringLiteral("secondary"),
                                           QStringLiteral("other")};
    const std::array<QString, 3> groupTexts{tr("%1 as the main position").arg(code),
                                            tr("%1 as a second position").arg(code),
                                            tr("Other positions")};
    struct Entry {
        const services::LineupCandidate* item;
        int group;
        bool available;
    };
    std::vector<Entry> entries;
    for (const services::LineupCandidate& item : m_data->candidates) {
        const core::Player& player = item.player;
        const int group = player.primaryPosition == slot.position ? 0
                          : std::ranges::find(player.secondaryPositions, slot.position) !=
                                  player.secondaryPositions.end()
                              ? 1
                              : 2;
        entries.push_back(
            {&item, group, item.facts.statusOnMatchDay == core::PlayerStatus::Available});
    }
    std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b) {
        if (a.group != b.group) {
            return a.group < b.group;
        }
        if (a.available != b.available) {
            return a.available;
        }
        const auto& left = a.item->player;
        const auto& right = b.item->player;
        if (left.shirtNumber.has_value() != right.shirtNumber.has_value()) {
            return left.shirtNumber.has_value();
        }
        if (left.shirtNumber != right.shirtNumber) {
            return *left.shirtNumber < *right.shirtNumber;
        }
        return SquadTexts::displayName(left).localeAwareCompare(SquadTexts::displayName(right)) < 0;
    });
    QList<QVariantList> rows;
    for (const Entry& entry : entries) {
        const core::Player& player = entry.item->player;
        const auto place = core::findPlayer(m_lineup, player.id);
        const QString placement = !place                                         ? QString{}
                                  : place->kind == core::LineupPlace::Kind::Slot ? kXi
                                                                                 : kBench;
        const auto load = m_loadStatuses.find(player.id);
        const auto group = static_cast<std::size_t>(entry.group);
        rows.append(QVariantList{
            QVariant(static_cast<qint64>(player.id.value)), shirtNumberOf(player),
            SquadTexts::displayName(player), positionsText(player), groupKeys[group],
            groupTexts[group], SquadTexts::statusKey(entry.item->facts.statusOnMatchDay, false),
            SquadTexts::statusName(entry.item->facts.statusOnMatchDay, false), entry.available,
            load == m_loadStatuses.end() ? QString{} : LoadTexts::statusKey(load->second),
            load == m_loadStatuses.end() ? QString{} : LoadTexts::statusShortName(load->second),
            placement,
            placement.isEmpty() ? tr("Not in the lineup") : LineupTexts::placementName(placement),
            slot.player == player.id});
    }
    m_picker->setRows(std::move(rows));
}

void PreMatchViewModel::updateDirty() {
    const bool dirty = m_data && m_lineup != m_baseline;
    if (dirty == m_dirty) {
        return;
    }
    m_dirty = dirty;
    emit dirtyChanged();
}

void PreMatchViewModel::setSelectedSlot(int slot) {
    if (slot == m_selectedSlot) {
        return;
    }
    m_selectedSlot = slot;
    emit selectionChanged();
}

void PreMatchViewModel::setPickerSlot(int slot) {
    if (slot == m_pickerSlot) {
        return;
    }
    m_pickerSlot = slot;
    emit pickerChanged();
}

void PreMatchViewModel::setLoadCheckFailed(bool failed) {
    if (failed == m_loadCheckFailed) {
        return;
    }
    m_loadCheckFailed = failed;
    emit loadPendingChanged();
    emit issuesChanged();
    emit textsChanged();
}

void PreMatchViewModel::rememberBeforeReplace() { m_beforeReplace = m_lineup; }

void PreMatchViewModel::setLoadPending(bool pending) {
    if (pending == m_loadPending) {
        return;
    }
    m_loadPending = pending;
    emit loadPendingChanged();
}

bool PreMatchViewModel::checkEditable() {
    if (!m_data) {
        return false;
    }
    if (readOnly()) {
        emit errorOccurred(readOnlyText());
        return false;
    }
    return true;
}

bool PreMatchViewModel::reportEdit(core::LineupEdit edit) {
    switch (edit) {
    case core::LineupEdit::Done:
        return true;
    case core::LineupEdit::Unchanged:
        return false;
    case core::LineupEdit::BenchFull:
        emit errorOccurred(tr("The bench is full: at most %1 players.").arg(core::kMaxBench));
        return false;
    case core::LineupEdit::GoalkeeperTaken:
        emit errorOccurred(tr("Only one slot can be in goal. Move the goalkeeper out first."));
        return false;
    case core::LineupEdit::InvalidSlot:
        qWarning("PreMatchViewModel: no such slot");
        return false;
    }
    return false;
}

void PreMatchViewModel::report(const core::Error& error) {
    qWarning("PreMatchViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

void PreMatchViewModel::reportDropped(int dropped) {
    if (dropped > 0) {
        emit notice(
            tr("Players no longer in the squad were left out of the lineup: %1.").arg(dropped));
    }
}

QString PreMatchViewModel::playerDetails(const services::LineupCandidate& item) const {
    QStringList lines{
        tr("On the match day: %1").arg(SquadTexts::statusName(item.facts.statusOnMatchDay, false))};
    if (m_loadCheckFailed) {
        lines.append(tr("Load: could not be checked"));
    } else if (const auto status = m_loadStatuses.find(item.player.id);
               status != m_loadStatuses.end()) {
        const core::Date asOf = m_loadAsOf.at(item.player.id);
        const QString day = LoadTexts::chartDay(asOf);
        const QString name = LoadTexts::statusShortName(status->second);
        lines.append(asOf == m_clock.today() ? tr("Load as of %1 (today): %2").arg(day, name)
                                             : tr("Load as of %1: %2").arg(day, name));
    }
    return lines.join(QLatin1Char('\n'));
}

QString PreMatchViewModel::playerName(core::PlayerId player) const {
    const auto* found = candidate(player);
    return found ? SquadTexts::displayName(found->player) : tr("Unknown player");
}

const services::LineupCandidate* PreMatchViewModel::candidate(core::PlayerId player) const {
    if (!m_data) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(
        m_data->candidates, [&](const auto& item) { return item.player.id == player; });
    return found == m_data->candidates.end() ? nullptr : &*found;
}

core::LineupContext PreMatchViewModel::contextWithLoad() const {
    core::LineupContext context = m_data ? m_data->context : core::LineupContext{};
    context.yellowCardThreshold = m_yellowCardThreshold;
    for (auto& [player, facts] : context.players) {
        const auto load = m_loadStatuses.find(player);
        facts.loadStatus = load == m_loadStatuses.end()
                               ? std::nullopt
                               : std::optional<core::LoadStatus>(load->second);
    }
    return context;
}

} // namespace fdss::viewmodels
