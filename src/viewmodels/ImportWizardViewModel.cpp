#include "viewmodels/ImportWizardViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "core/parsing/HeaderMatching.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ImportTexts.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/RowsModel.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <utility>

namespace fdss::viewmodels {

namespace {

const QString kTypeStep = QStringLiteral("type");
const QString kFileStep = QStringLiteral("file");
const QString kPreviewStep = QStringLiteral("preview");
const QString kMappingStep = QStringLiteral("mapping");
const QString kReportStep = QStringLiteral("report");
const QString kSummaryStep = QStringLiteral("summary");
const QString kSquadType = QStringLiteral("squad");
const QString kTrainingType = QStringLiteral("training");
const QString kMatchesType = QStringLiteral("matches");
const QString kSkipPolicy = QStringLiteral("skip");
const QString kCreatePolicy = QStringLiteral("create");

// Packet P16, requirement 1.
constexpr int kPreviewRows = 20;
constexpr int kNameSplitRows = 5;
constexpr int kSamples = 3;

QString fromStd(const std::string& text) { return QString::fromStdString(text); }

QString columnTitle(const std::vector<std::string>& headers, std::size_t column) {
    const QString header = fromStd(headers[column]);
    return header.isEmpty()
               ? QCoreApplication::translate("fdss::viewmodels::ImportWizardViewModel", "Column %1")
                     .arg(column + 1)
               : header;
}

std::optional<std::size_t> columnOf(const core::SquadMapping& mapping, core::SquadField field) {
    const auto found = std::ranges::find(mapping.columns, std::optional(field));
    if (found == mapping.columns.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(mapping.columns.begin(), found));
}

core::Date localDate(core::Timestamp timestamp) {
    const QDate date =
        QDateTime::fromSecsSinceEpoch(timestamp.time_since_epoch().count(), QTimeZone::UTC)
            .toLocalTime()
            .date();
    return core::Date{std::chrono::year{date.year()},
                      std::chrono::month{static_cast<unsigned>(date.month())},
                      std::chrono::day{static_cast<unsigned>(date.day())}};
}

std::optional<services::ImportKind> kindOf(const QString& type) {
    if (type == kSquadType) {
        return services::ImportKind::Squad;
    }
    if (type == kTrainingType) {
        return services::ImportKind::Training;
    }
    if (type == kMatchesType) {
        return services::ImportKind::Matches;
    }
    return std::nullopt;
}

std::string fieldKey(services::ImportKind kind, int field) {
    if (kind == services::ImportKind::Matches) {
        return std::string(core::toKey(static_cast<core::MatchField>(field)));
    }
    return std::string(core::toKey(static_cast<core::TrainingField>(field)));
}

std::optional<int> fieldFromKey(services::ImportKind kind, const std::string& key) {
    if (kind == services::ImportKind::Matches) {
        const auto field = core::matchFieldFromKey(key);
        return field ? std::optional(static_cast<int>(*field)) : std::nullopt;
    }
    const auto field = core::trainingFieldFromKey(key);
    return field ? std::optional(static_cast<int>(*field)) : std::nullopt;
}

// The mapping choice of a column: "", a field key or "metric:<key>".
QString choiceOf(services::ImportKind kind, const core::ColumnTarget& target,
                 std::span<const core::MetricDefinition> metrics) {
    if (target.field) {
        return QString::fromStdString(fieldKey(kind, *target.field));
    }
    if (target.metric) {
        const auto metric = std::ranges::find(metrics, *target.metric, &core::MetricDefinition::id);
        if (metric != metrics.end()) {
            return QString::fromUtf8(kMetricChoicePrefix) + QString::fromStdString(metric->key);
        }
    }
    return {};
}

int playerField(services::ImportKind kind) {
    return kind == services::ImportKind::Matches ? static_cast<int>(core::MatchField::Player)
                                                 : static_cast<int>(core::TrainingField::Player);
}

} // namespace

ImportWizardViewModel::ImportWizardViewModel(QObject* parent)
    : QObject(parent), m_imports(ServiceRegistry::instance().imports()),
      m_session(ServiceRegistry::instance().session()),
      m_mapping(new RowsModel({"column", "header", "samples", "field", "autoMatched"}, this)),
      m_report(new RowsModel(
          {"rowNumber", "statusKey", "statusText", "name", "action", "details", "message"}, this)) {
    // As LoadViewModel: QEvent::LanguageChange reaches only an application-wide filter.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

ImportWizardViewModel::~ImportWizardViewModel() = default;

qint64 ImportWizardViewModel::teamId() const { return m_teamId; }

void ImportWizardViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    // The report compared the rows with the players of the other team.
    if (m_step == kReportStep || (m_busy && m_validation)) {
        invalidateValidation();
    }
}

QString ImportWizardViewModel::step() const { return m_step; }

bool ImportWizardViewModel::canImport() const {
    return m_session.isAllowed(core::Permission::Import);
}

bool ImportWizardViewModel::busy() const { return m_busy; }

double ImportWizardViewModel::progress() const { return m_progress; }

QString ImportWizardViewModel::errorText() const { return m_errorText; }

QVariantList ImportWizardViewModel::typeOptions() const {
    return {QVariantMap{
                {QStringLiteral("value"), kSquadType},
                {QStringLiteral("text"), tr("Squad")},
                {QStringLiteral("description"),
                 tr("Players with their names, shirt numbers, birth years, positions and foot.")}},
            QVariantMap{{QStringLiteral("value"), kTrainingType},
                        {QStringLiteral("text"), tr("Training")},
                        {QStringLiteral("description"),
                         tr("Attendance, RPE and minutes of every player per session, and custom "
                            "metrics: the history for load monitoring.")}},
            QVariantMap{{QStringLiteral("value"), kMatchesType},
                        {QStringLiteral("text"), tr("Matches")},
                        {QStringLiteral("description"),
                         tr("Results with the minutes, ratings and RPE of every player per match, "
                            "and custom metrics.")}}};
}

QString ImportWizardViewModel::type() const { return m_type; }

QString ImportWizardViewModel::metricScope() const {
    return m_kind == services::ImportKind::Matches ? QStringLiteral("match")
                                                   : QStringLiteral("training");
}

bool ImportWizardViewModel::canManageMetrics() const {
    return m_session.isAllowed(core::Permission::ManageMetrics);
}

bool ImportWizardViewModel::activity() const { return m_kind != services::ImportKind::Squad; }

QString ImportWizardViewModel::fileName() const {
    return m_source ? m_source->fileName : QString();
}

QString ImportWizardViewModel::sourceSummary() const {
    if (!m_source) {
        return {};
    }
    const int rows = rowCount();
    if (m_source->kind == services::ImportFileKind::Xlsx) {
        return tr("XLSX · %n row(s)", nullptr, rows);
    }
    return tr("CSV · %1 · %2 · %n row(s)", nullptr, rows)
        .arg(ImportTexts::encodingName(m_source->encoding),
             ImportTexts::delimiterName(m_source->delimiter));
}

QVariantList ImportWizardViewModel::sheetOptions() const {
    QVariantList options;
    if (!m_source) {
        return options;
    }
    for (int index = 0; index < m_source->sheets.size(); ++index) {
        options.append(QVariantMap{{QStringLiteral("value"), index},
                                   {QStringLiteral("text"), m_source->sheets.at(index)}});
    }
    return options;
}

int ImportWizardViewModel::sheetIndex() const { return m_source ? m_source->sheet : 0; }

QVariantList ImportWizardViewModel::previewColumns() const {
    QVariantList columns;
    if (!m_source || !m_source->tableStatus) {
        return columns;
    }
    columns.append(QVariantMap{{QStringLiteral("role"), QStringLiteral("rowNumber")},
                               {QStringLiteral("title"), tr("Row")}});
    const auto& headers = m_source->table.headers;
    for (std::size_t column = 0; column < headers.size(); ++column) {
        columns.append(QVariantMap{{QStringLiteral("role"), QStringLiteral("c%1").arg(column)},
                                   {QStringLiteral("title"), columnTitle(headers, column)}});
    }
    return columns;
}

QVariantList ImportWizardViewModel::previewRows() const {
    QVariantList rows;
    if (!m_source || !m_source->tableStatus) {
        return rows;
    }
    const auto& table = m_source->table;
    const std::size_t shown = std::min<std::size_t>(table.rows.size(), kPreviewRows);
    for (std::size_t index = 0; index < shown; ++index) {
        const core::SourceRow& row = table.rows[index];
        QVariantMap values{{QStringLiteral("rowNumber"), row.number}};
        for (std::size_t column = 0; column < table.headers.size(); ++column) {
            values.insert(QStringLiteral("c%1").arg(column), fromStd(core::cellAt(row, column)));
        }
        rows.append(values);
    }
    return rows;
}

int ImportWizardViewModel::rowCount() const {
    return m_source && m_source->tableStatus ? static_cast<int>(m_source->table.rows.size()) : 0;
}

QString ImportWizardViewModel::duplicateWarning() const { return previousImportText(); }

RowsModel* ImportWizardViewModel::mapping() const { return m_mapping; }

QVariantList ImportWizardViewModel::fieldOptions() const {
    if (activity()) {
        return ImportActivityTexts::fieldOptions(m_kind, m_metrics, canManageMetrics());
    }
    return ImportTexts::fieldOptions();
}

bool ImportWizardViewModel::mappingRemembered() const { return m_mappingRemembered; }

bool ImportWizardViewModel::nameOrderVisible() const {
    if (activity()) {
        return m_options.unknownPlayers == core::UnknownPlayerPolicy::Create;
    }
    return columnOf(m_squadMapping, core::SquadField::FullName).has_value();
}

QString ImportWizardViewModel::nameOrder() const {
    return QString::fromUtf8(
        core::toKey(activity() ? m_options.nameOrder : m_squadMapping.nameOrder));
}

QVariantList ImportWizardViewModel::nameOrderOptions() const {
    return ImportTexts::nameOrderOptions();
}

QVariantList ImportWizardViewModel::nameSplitPreview() const {
    QVariantList rows;
    if (activity()) {
        // The new players of the plan, as the order splits them.
        const std::vector<core::NewPlayer>* players =
            m_trainingPlan ? &m_trainingPlan->newPlayers
                           : (m_matchPlan ? &m_matchPlan->newPlayers : nullptr);
        if (players == nullptr) {
            return rows;
        }
        for (const core::NewPlayer& player : *players) {
            if (rows.size() >= kNameSplitRows) {
                break;
            }
            const QString first = fromStd(player.draft.firstName);
            const QString last = fromStd(player.draft.lastName);
            const QString full = m_options.nameOrder == core::NameOrder::FirstLast
                                     ? first + QLatin1Char(' ') + last
                                     : last + QLatin1Char(' ') + first;
            rows.append(QVariantMap{{QStringLiteral("fullName"), full},
                                    {QStringLiteral("firstName"), first},
                                    {QStringLiteral("lastName"), last},
                                    {QStringLiteral("split"), true}});
        }
        return rows;
    }
    const auto column = columnOf(m_squadMapping, core::SquadField::FullName);
    if (!m_source || !m_source->tableStatus || !column) {
        return rows;
    }
    for (const core::SourceRow& row : m_source->table.rows) {
        if (rows.size() >= kNameSplitRows) {
            break;
        }
        const std::string& full = core::cellAt(row, *column);
        const auto parts = core::splitFullName(full, m_squadMapping.nameOrder);
        rows.append(QVariantMap{
            {QStringLiteral("fullName"), fromStd(full)},
            {QStringLiteral("firstName"), parts ? fromStd(parts->firstName) : QString()},
            {QStringLiteral("lastName"), parts ? fromStd(parts->lastName) : QString()},
            {QStringLiteral("split"), parts.has_value()}});
    }
    return rows;
}

bool ImportWizardViewModel::defaultPositionRequired() const {
    if (activity()) {
        return m_options.unknownPlayers == core::UnknownPlayerPolicy::Create;
    }
    return m_source && m_source->tableStatus &&
           core::needsDefaultPosition(m_squadMapping, m_source->table);
}

QString ImportWizardViewModel::defaultPosition() const {
    const auto& position = activity() ? m_options.defaultPosition : m_squadMapping.defaultPosition;
    return position ? QString::fromUtf8(core::toDbText(*position)) : QString();
}

QVariantList ImportWizardViewModel::positionOptions() const {
    return SquadTexts::optionalPositionOptions();
}

QStringList ImportWizardViewModel::mappingErrors() const {
    QStringList errors;
    if (!m_source || !m_source->tableStatus) {
        return errors;
    }
    if (activity()) {
        const auto issues = m_kind == services::ImportKind::Matches
                                ? core::checkMatchMapping(m_activityMapping)
                                : core::checkTrainingMapping(m_activityMapping);
        for (const core::ActivityMappingIssue issue : issues) {
            errors.append(ImportActivityTexts::mappingIssue(issue));
        }
        return errors;
    }
    for (const core::MappingIssue issue : core::checkMapping(m_squadMapping)) {
        errors.append(ImportTexts::mappingIssue(issue));
    }
    return errors;
}

RowsModel* ImportWizardViewModel::report() const { return m_report; }

namespace {

int countRows(const std::vector<core::SquadRow>& rows, core::ExistingPolicy policy,
              core::RowStatus status) {
    return static_cast<int>(std::ranges::count_if(
        rows, [&](const core::SquadRow& row) { return core::statusFor(row, policy) == status; }));
}

int countActions(const std::vector<core::SquadRow>& rows, core::ExistingPolicy policy,
                 std::initializer_list<core::RowAction> actions) {
    return static_cast<int>(std::ranges::count_if(rows, [&](const core::SquadRow& row) {
        return std::ranges::find(actions, core::actionFor(row, policy)) != actions.end();
    }));
}

int countActivityRows(const std::vector<ActivityReportRow>& rows, core::RowStatus status) {
    return static_cast<int>(std::ranges::count(rows, status, &ActivityReportRow::status));
}

int countOf(const std::vector<core::ActivityAction>& actions,
            std::initializer_list<core::ActivityAction> wanted) {
    return static_cast<int>(std::ranges::count_if(actions, [&](core::ActivityAction action) {
        return std::ranges::find(wanted, action) != wanted.end();
    }));
}

} // namespace

std::vector<core::ActivityAction> ImportWizardViewModel::activityActions() const {
    std::vector<core::ActivityAction> actions;
    if (m_trainingPlan) {
        for (const core::PlannedTrainingRow& row : m_trainingPlan->rows) {
            actions.push_back(row.action);
        }
    }
    if (m_matchPlan) {
        for (const core::PlannedMatchRow& row : m_matchPlan->rows) {
            actions.push_back(row.action);
        }
    }
    return actions;
}

int ImportWizardViewModel::okCount() const {
    if (activity()) {
        return countActivityRows(m_activityRows, core::RowStatus::Ok);
    }
    return countRows(m_rows, existingPolicy(), core::RowStatus::Ok);
}

int ImportWizardViewModel::warningCount() const {
    if (activity()) {
        return countActivityRows(m_activityRows, core::RowStatus::Warning);
    }
    return countRows(m_rows, existingPolicy(), core::RowStatus::Warning);
}

int ImportWizardViewModel::errorCount() const {
    if (activity()) {
        return countActivityRows(m_activityRows, core::RowStatus::Error);
    }
    return countRows(m_rows, existingPolicy(), core::RowStatus::Error);
}

int ImportWizardViewModel::createCount() const {
    if (activity()) {
        return countOf(activityActions(), {core::ActivityAction::Create});
    }
    return countActions(m_rows, existingPolicy(), {core::RowAction::Create});
}

int ImportWizardViewModel::updateCount() const {
    if (activity()) {
        return countOf(activityActions(), {core::ActivityAction::Update});
    }
    return countActions(m_rows, existingPolicy(), {core::RowAction::Update});
}

int ImportWizardViewModel::skipCount() const {
    if (activity()) {
        return countOf(activityActions(),
                       {core::ActivityAction::Skip, core::ActivityAction::Unchanged,
                        core::ActivityAction::KeepExisting});
    }
    return countActions(m_rows, existingPolicy(),
                        {core::RowAction::SkipExisting, core::RowAction::SkipAmbiguous,
                         core::RowAction::SkipExample});
}

bool ImportWizardViewModel::updateExisting() const { return m_updateExisting; }

void ImportWizardViewModel::setUpdateExisting(bool update) {
    if (update == m_updateExisting) {
        return;
    }
    m_updateExisting = update;
    m_options.updateExisting = update;
    if (m_busy && m_validation) {
        invalidateValidation();
    }
    if (activity()) {
        replan();
        return;
    }
    rebuildReport();
}

bool ImportWizardViewModel::skipInvalid() const { return m_skipInvalid; }

void ImportWizardViewModel::setSkipInvalid(bool skip) {
    if (skip == m_skipInvalid) {
        return;
    }
    m_skipInvalid = skip;
    m_options.skipInvalid = skip;
    if (m_busy && m_validation) {
        invalidateValidation();
    }
    emit reportChanged();
}

bool ImportWizardViewModel::canCommit() const {
    return !m_busy && m_step == kReportStep && m_source && m_source->tableStatus && canImport() &&
           createCount() + updateCount() > 0 && (errorCount() == 0 || m_skipInvalid);
}

QString ImportWizardViewModel::unknownPlayers() const {
    return m_options.unknownPlayers == core::UnknownPlayerPolicy::Create ? kCreatePolicy
                                                                         : kSkipPolicy;
}

void ImportWizardViewModel::setUnknownPlayers(const QString& policy) {
    if (policy != kSkipPolicy && policy != kCreatePolicy) {
        return;
    }
    const auto value = policy == kCreatePolicy ? core::UnknownPlayerPolicy::Create
                                               : core::UnknownPlayerPolicy::Skip;
    if (value == m_options.unknownPlayers) {
        return;
    }
    m_options.unknownPlayers = value;
    replan();
    emit mappingChanged();
}

int ImportWizardViewModel::newPlayerCount() const {
    if (m_trainingPlan) {
        return static_cast<int>(m_trainingPlan->newPlayers.size());
    }
    return m_matchPlan ? static_cast<int>(m_matchPlan->newPlayers.size()) : 0;
}

core::NotImportedEvents ImportWizardViewModel::plannedEvents() const {
    if (m_activitySummary) {
        return m_activitySummary->notImported;
    }
    if (m_matchImport && m_matchPlan) {
        return core::notImportedEvents(*m_matchImport, *m_matchPlan);
    }
    return {};
}

QString ImportWizardViewModel::notImportedText() const {
    return ImportActivityTexts::notImported(plannedEvents());
}

QString ImportWizardViewModel::suspensionNote() const {
    return ImportActivityTexts::suspensionNote(plannedEvents());
}

QVariantMap ImportWizardViewModel::summary() const {
    QVariantMap values;
    if (m_activitySummary) {
        const services::ActivityImportSummary& done = *m_activitySummary;
        values.insert(QStringLiteral("created"), done.created);
        values.insert(QStringLiteral("updated"), done.updated);
        values.insert(QStringLiteral("skipped"), done.skipped + done.unchanged + done.kept);
        values.insert(QStringLiteral("invalid"), done.invalid);
        values.insert(QStringLiteral("sessionsCreated"), done.sessionsCreated);
        values.insert(QStringLiteral("matchesCreated"), done.matchesCreated);
        values.insert(QStringLiteral("playersCreated"), done.playersCreated);
        values.insert(QStringLiteral("opponentsCreated"), done.opponentsCreated);
        values.insert(QStringLiteral("metricValues"), done.metricValues);
        if (m_undo) {
            values.insert(QStringLiteral("deleted"), m_undo->deleted);
            values.insert(QStringLiteral("archived"), m_undo->archived);
            values.insert(QStringLiteral("notReverted"), m_undo->notReverted);
            values.insert(QStringLiteral("sessionsDeleted"), m_undo->sessionsDeleted);
            values.insert(QStringLiteral("matchesDeleted"), m_undo->matchesDeleted);
            values.insert(QStringLiteral("rowsDeleted"), m_undo->rowsDeleted);
            values.insert(QStringLiteral("rowsRestored"), m_undo->rowsRestored);
        }
        return values;
    }
    if (!m_summary) {
        return values;
    }
    values.insert(QStringLiteral("created"), m_summary->created);
    values.insert(QStringLiteral("updated"), m_summary->updated);
    values.insert(QStringLiteral("skipped"), m_summary->skippedExisting +
                                                 m_summary->skippedAmbiguous +
                                                 m_summary->skippedExamples);
    values.insert(QStringLiteral("invalid"), m_summary->invalid);
    if (m_undo) {
        values.insert(QStringLiteral("deleted"), m_undo->deleted);
        values.insert(QStringLiteral("archived"), m_undo->archived);
        values.insert(QStringLiteral("notReverted"), m_undo->notReverted);
    }
    return values;
}

bool ImportWizardViewModel::canUndo() const {
    return (m_summary.has_value() || m_activitySummary.has_value()) && !m_undo.has_value() &&
           canImport();
}

bool ImportWizardViewModel::undone() const { return m_undo.has_value(); }

QString ImportWizardViewModel::summaryText() const {
    if (!m_activitySummary) {
        return {};
    }
    const services::ActivityImportSummary& done = *m_activitySummary;
    QStringList lines;
    if (m_kind == services::ImportKind::Matches) {
        lines.append(tr("Matches added: %1. Opponents added: %2.")
                         .arg(done.matchesCreated)
                         .arg(done.opponentsCreated));
    } else {
        lines.append(tr("Sessions added: %1.").arg(done.sessionsCreated));
    }
    lines.append(tr("Rows added: %1. Updated: %2. Skipped or already in the app: %3. Not imported "
                    "because of errors: %4.")
                     .arg(done.created)
                     .arg(done.updated)
                     .arg(done.skipped + done.unchanged + done.kept)
                     .arg(done.invalid));
    lines.append(tr("Players added to the squad: %1. Metric values saved: %2.")
                     .arg(done.playersCreated)
                     .arg(done.metricValues));
    return lines.join(QLatin1Char(' '));
}

QString ImportWizardViewModel::undoText() const {
    if (!m_activitySummary) {
        return {};
    }
    if (!m_undo) {
        return m_kind == services::ImportKind::Matches
                   ? tr("Undo import removes the matches, appearances, opponents, players and "
                        "metric values this import added, and puts values it overwrote back.")
                   : tr("Undo import removes the sessions, attendance, players and metric values "
                        "this import added, and puts values it overwrote back.");
    }
    return tr("Removed: %1 session(s), %2 match(es), %3 row(s) in sessions or matches that stay, "
              "%4 player(s). Values put back: %5. Not changed back, because they were edited after "
              "the import: %6.")
        .arg(m_undo->sessionsDeleted)
        .arg(m_undo->matchesDeleted)
        .arg(m_undo->rowsDeleted)
        .arg(m_undo->deleted)
        .arg(m_undo->rowsRestored)
        .arg(m_undo->notReverted);
}

void ImportWizardViewModel::chooseType(const QString& type) {
    const auto kind = kindOf(type);
    if (!kind || !canImport()) {
        return;
    }
    if (type != m_type) {
        m_type = type;
        m_kind = *kind;
        emit typeChanged();
        emit textsChanged();
    }
    setStep(kFileStep);
}

void ImportWizardViewModel::openFile(const QUrl& file) {
    openPath(file.isLocalFile() ? file.toLocalFile() : file.toString());
}

void ImportWizardViewModel::openPath(const QString& path) { read(path, std::nullopt); }

void ImportWizardViewModel::selectSheet(int index) {
    if (!m_source || index == m_source->sheet || index < 0 || index >= m_source->sheets.size()) {
        return;
    }
    read(m_source->path, index);
}

void ImportWizardViewModel::goToMapping() {
    if (m_step == kPreviewStep && m_source && m_source->tableStatus && !m_busy) {
        setStep(kMappingStep);
    }
}

void ImportWizardViewModel::setColumnField(int column, const QString& field) {
    if (activity()) {
        setActivityColumn(column, field);
        return;
    }
    if (!m_source || column < 0 || column >= static_cast<int>(m_squadMapping.columns.size())) {
        return;
    }
    std::optional<core::SquadField> value;
    if (!field.isEmpty()) {
        value = core::squadFieldFromKey(field.toStdString());
        if (!value) {
            qWarning("ImportWizardViewModel: unknown field '%s'", qUtf8Printable(field));
            return;
        }
    }
    auto& columns = m_squadMapping.columns;
    if (value) {
        // One column per field; a full name and its parts exclude each other.
        const bool full = *value == core::SquadField::FullName;
        const bool part =
            *value == core::SquadField::FirstName || *value == core::SquadField::LastName;
        for (auto& other : columns) {
            if (other == value ||
                (full &&
                 (other == core::SquadField::FirstName || other == core::SquadField::LastName)) ||
                (part && other == core::SquadField::FullName)) {
                other.reset();
            }
        }
        if (full) {
            m_squadMapping.nameOrder =
                core::detectNameOrder(m_source->table.headers[static_cast<std::size_t>(column)]);
        }
    }
    columns[static_cast<std::size_t>(column)] = value;
    invalidateValidation();
    rebuildMappingRows();
    emit mappingChanged();
}

void ImportWizardViewModel::setActivityColumn(int column, const QString& field) {
    if (!m_source || column < 0 || column >= static_cast<int>(m_activityMapping.columns.size())) {
        return;
    }
    const auto index = static_cast<std::size_t>(column);
    if (field == QLatin1StringView(kCreateMetricChoice)) {
        if (canManageMetrics()) {
            emit createMetricRequested(column, fromStd(m_source->table.headers[index]));
        }
        rebuildMappingRows(); // the choice shows the column as it was
        return;
    }
    core::ColumnTarget target;
    if (field.startsWith(QLatin1StringView(kMetricChoicePrefix))) {
        const std::string key =
            field.mid(QLatin1StringView(kMetricChoicePrefix).size()).toStdString();
        const auto metric = std::ranges::find(m_metrics, key, &core::MetricDefinition::key);
        if (metric == m_metrics.end()) {
            qWarning("ImportWizardViewModel: unknown metric '%s'", qUtf8Printable(field));
            return;
        }
        target.metric = metric->id;
    } else if (!field.isEmpty()) {
        target.field = fieldFromKey(m_kind, field.toStdString());
        if (!target.field) {
            qWarning("ImportWizardViewModel: unknown field '%s'", qUtf8Printable(field));
            return;
        }
    }
    // One column per field and per metric.
    for (core::ColumnTarget& other : m_activityMapping.columns) {
        if ((target.field && other.field == target.field) ||
            (target.metric && other.metric == target.metric)) {
            other = {};
        }
    }
    m_activityMapping.columns[index] = target;
    if (target.field == playerField(m_kind)) {
        m_options.nameOrder = core::detectNameOrder(m_source->table.headers[index]);
    }
    invalidateValidation();
    rebuildMappingRows();
    emit mappingChanged();
}

void ImportWizardViewModel::metricCreated(int column) {
    if (!activity() || !m_source) {
        return;
    }
    auto metrics = m_imports.metricsFor(m_kind);
    if (!metrics) {
        emit errorOccurred(ErrorPresenter::message(metrics.error()));
        return;
    }
    std::optional<core::MetricDefinitionId> created;
    for (const core::MetricDefinition& metric : *metrics) {
        if (std::ranges::find(m_metrics, metric.id, &core::MetricDefinition::id) ==
            m_metrics.end()) {
            created = metric.id;
        }
    }
    m_metrics = std::move(*metrics);
    emit textsChanged();
    if (created && column >= 0 && column < static_cast<int>(m_activityMapping.columns.size())) {
        const auto metric = std::ranges::find(m_metrics, *created, &core::MetricDefinition::id);
        setActivityColumn(column, QString::fromUtf8(kMetricChoicePrefix) + fromStd(metric->key));
        return;
    }
    rebuildMappingRows();
    emit mappingChanged();
}

void ImportWizardViewModel::setNameOrder(const QString& order) {
    const auto value = core::nameOrderFromKey(order.toStdString());
    if (activity()) {
        if (!value || *value == m_options.nameOrder) {
            return;
        }
        m_options.nameOrder = *value;
        replan();
        emit mappingChanged();
        return;
    }
    if (!value || *value == m_squadMapping.nameOrder) {
        return;
    }
    m_squadMapping.nameOrder = *value;
    invalidateValidation();
    emit mappingChanged();
}

void ImportWizardViewModel::setDefaultPosition(const QString& position) {
    std::optional<core::Position> value;
    if (!position.isEmpty()) {
        auto parsed = core::fromDbText<core::Position>(position.toStdString());
        if (!parsed) {
            return;
        }
        value = *parsed;
    }
    if (activity()) {
        if (value == m_options.defaultPosition) {
            return;
        }
        m_options.defaultPosition = value;
        replan();
        emit mappingChanged();
        return;
    }
    if (value == m_squadMapping.defaultPosition) {
        return;
    }
    m_squadMapping.defaultPosition = value;
    invalidateValidation();
    emit mappingChanged();
}

void ImportWizardViewModel::validate() {
    if (m_busy || m_step != kMappingStep || !m_source || !m_source->tableStatus ||
        !mappingErrors().isEmpty()) {
        return;
    }
    if (m_teamId == 0) {
        emit errorOccurred(tr("No team is selected. Set up the team first."));
        return;
    }
    const int request = ++m_request;
    const core::TeamId team{m_teamId};
    if (m_kind == services::ImportKind::Training) {
        auto future = m_imports.validateTraining(team, *m_source, m_activityMapping);
        if (!future) {
            emit errorOccurred(ErrorPresenter::message(future.error()));
            return;
        }
        auto* watcher = new QFutureWatcher<core::TrainingImport>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, request] {
            if (request != m_request || watcher->future().resultCount() == 0) {
                return;
            }
            m_trainingImport = watcher->result();
            m_validation = nullptr;
            watcher->deleteLater();
            setBusy(false);
            replan();
            setStep(kReportStep);
        });
        startValidation(watcher, request);
        watcher->setFuture(*future);
        return;
    }
    if (m_kind == services::ImportKind::Matches) {
        auto future = m_imports.validateMatches(team, *m_source, m_activityMapping);
        if (!future) {
            emit errorOccurred(ErrorPresenter::message(future.error()));
            return;
        }
        auto* watcher = new QFutureWatcher<core::MatchImport>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, request] {
            if (request != m_request || watcher->future().resultCount() == 0) {
                return;
            }
            m_matchImport = watcher->result();
            m_validation = nullptr;
            watcher->deleteLater();
            setBusy(false);
            replan();
            setStep(kReportStep);
        });
        startValidation(watcher, request);
        watcher->setFuture(*future);
        return;
    }
    auto future = m_imports.validateSquad(team, *m_source, m_squadMapping);
    if (!future) {
        emit errorOccurred(ErrorPresenter::message(future.error()));
        return;
    }
    auto* watcher = new QFutureWatcher<std::vector<core::SquadRow>>(this);
    startValidation(watcher, request);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, request] {
        if (request != m_request || watcher->future().resultCount() == 0) {
            return;
        }
        m_rows = watcher->result();
        m_validation = nullptr;
        watcher->deleteLater();
        setBusy(false);
        rebuildReport();
        setStep(kReportStep);
    });
    watcher->setFuture(*future);
}

void ImportWizardViewModel::startValidation(QFutureWatcherBase* watcher, int request) {
    setBusy(true, 0);
    delete m_validation;
    m_validation = watcher;
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this,
            [this, watcher, request](int value) {
                if (request != m_request) {
                    return;
                }
                const int maximum = watcher->progressMaximum();
                m_progress = maximum > 0 ? static_cast<double>(value) / maximum : 0;
                emit progressChanged();
            });
}

void ImportWizardViewModel::replan() {
    const core::TeamId team{m_teamId};
    if (m_kind == services::ImportKind::Training && m_trainingImport) {
        auto plan = m_imports.planTraining(team, *m_trainingImport, m_activityMapping, m_options);
        if (plan) {
            m_trainingPlan = std::move(*plan);
        } else {
            m_trainingPlan.reset();
            emit errorOccurred(ErrorPresenter::message(plan.error()));
        }
    } else if (m_kind == services::ImportKind::Matches && m_matchImport) {
        auto plan = m_imports.planMatches(team, *m_matchImport, m_activityMapping, m_options);
        if (plan) {
            m_matchPlan = std::move(*plan);
        } else {
            m_matchPlan.reset();
            emit errorOccurred(ErrorPresenter::message(plan.error()));
        }
    }
    rebuildReport();
}

void ImportWizardViewModel::commit() {
    if (!canCommit()) {
        return;
    }
    if (activity()) {
        const core::TeamId team{m_teamId};
        auto result = m_kind == services::ImportKind::Training
                          ? m_imports.commitTraining(team, *m_source, m_activityMapping,
                                                     *m_trainingImport, *m_trainingPlan, m_options)
                          : m_imports.commitMatches(team, *m_source, m_activityMapping,
                                                    *m_matchImport, *m_matchPlan, m_options);
        if (!result) {
            // The data changed since the check: the report shows the new plan.
            if (result.error().code == core::ErrorCode::Conflict) {
                replan();
            }
            emit errorOccurred(ErrorPresenter::message(result.error()));
            return;
        }
        m_activitySummary = *result;
        m_undo.reset();
        emit summaryChanged();
        emit reportChanged();
        emit squadChanged();
        setStep(kSummaryStep);
        return;
    }
    const services::SquadImportOptions options{existingPolicy(),
                                               m_skipInvalid ? services::InvalidRows::Skip
                                                             : services::InvalidRows::Abort};
    auto result =
        m_imports.commitSquad(core::TeamId{m_teamId}, *m_source, m_squadMapping, m_rows, options);
    rebuildReport();
    if (!result) {
        emit errorOccurred(ErrorPresenter::message(result.error()));
        return;
    }
    m_summary = *result;
    m_undo.reset();
    emit summaryChanged();
    emit squadChanged();
    setStep(kSummaryStep);
}

void ImportWizardViewModel::undoImport() {
    if (!canUndo()) {
        return;
    }
    if (m_activitySummary) {
        // Packet P18 decision 10: data added after the import is named before it goes.
        auto preview = m_imports.undoPreview(m_activitySummary->importId);
        if (!preview) {
            emit errorOccurred(ErrorPresenter::message(preview.error()));
            return;
        }
        if (preview->any()) {
            emit undoConfirmationRequested(ImportActivityTexts::undoQuestion(*preview));
            return;
        }
    }
    performUndo();
}

void ImportWizardViewModel::confirmUndo() {
    if (canUndo()) {
        performUndo();
    }
}

void ImportWizardViewModel::performUndo() {
    const core::ImportId id = m_activitySummary ? m_activitySummary->importId : m_summary->importId;
    auto result = m_imports.undo(id);
    if (!result) {
        emit errorOccurred(ErrorPresenter::message(result.error()));
        return;
    }
    m_undo = *result;
    emit summaryChanged();
    emit squadChanged();
    emit infoMessage(tr("The import was undone."));
}

void ImportWizardViewModel::back() {
    if (m_busy) {
        return;
    }
    if (m_step == kReportStep) {
        clearReport();
        setStep(kMappingStep);
    } else if (m_step == kMappingStep) {
        setStep(kPreviewStep);
    } else if (m_step == kPreviewStep) {
        setErrorText({});
        m_error.reset();
        m_errorLine.reset();
        setStep(kFileStep);
    } else if (m_step == kFileStep) {
        setErrorText({});
        m_error.reset();
        m_errorLine.reset();
        setStep(kTypeStep);
    }
}

void ImportWizardViewModel::reset() {
    invalidateValidation();
    setErrorText({});
    m_error.reset();
    m_errorLine.reset();
    m_source.reset();
    m_previous.clear();
    m_squadMapping = {};
    m_mappingRemembered = false;
    m_updateExisting = false;
    m_skipInvalid = true;
    m_summary.reset();
    m_undo.reset();
    m_activityMapping = {};
    m_metrics.clear();
    m_options = {};
    m_activitySummary.reset();
    clearReport();
    rebuildMappingRows();
    emit sourceChanged();
    emit mappingChanged();
    emit summaryChanged();
    setStep(kTypeStep);
}

bool ImportWizardViewModel::saveTemplate(const QUrl& target, const QString& format,
                                         const QString& type) {
    const QString chosen = type.isEmpty() ? m_type : type;
    if (!kindOf(chosen)) {
        return false;
    }
    const QString source =
        QStringLiteral(":/fdss/import-templates/%1.%2")
            .arg(chosen, format == QLatin1StringView("xlsx") ? QStringLiteral("xlsx")
                                                             : QStringLiteral("csv"));
    const QString path = target.isLocalFile() ? target.toLocalFile() : target.toString();
    QFile resource(source);
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    const bool readable = resource.open(QIODevice::ReadOnly);
    const QByteArray bytes = readable ? resource.readAll() : QByteArray{};
    if (!readable || resource.error() != QFile::NoError || !output.open(QIODevice::WriteOnly) ||
        output.write(bytes) != bytes.size() || !output.commit()) {
        qWarning("ImportWizardViewModel: cannot write the template to %s", qUtf8Printable(path));
        emit errorOccurred(ErrorPresenter::message(
            core::Error{core::ErrorCode::Io, "cannot write the template", "import_template"}));
        return false;
    }
    emit infoMessage(tr("The template was saved. Fill it in and import it here."));
    return true;
}

QVariantMap ImportWizardViewModel::reportRow(int row) const { return m_report->get(row); }

bool ImportWizardViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        if (m_error) {
            setErrorText(errorMessage());
        }
        rebuildMappingRows();
        rebuildReport();
        emit textsChanged();
        emit sourceChanged();
        emit mappingChanged();
        emit summaryChanged();
    }
    return QObject::eventFilter(watched, event);
}

void ImportWizardViewModel::read(const QString& path, std::optional<int> sheet) {
    if (!canImport()) {
        return;
    }
    invalidateValidation();
    const int request = ++m_request;
    setErrorText({});
    m_error.reset();
    m_errorLine.reset();
    auto future = m_imports.read(path, sheet, m_kind);
    if (!future) {
        failWith(future.error());
        return;
    }
    setBusy(true, -1);
    future->then(this, [this, request](core::Result<services::ImportSource> source) {
        if (request != m_request) {
            return;
        }
        setBusy(false);
        if (!source) {
            failWith(source.error());
            return;
        }
        applySource(std::move(*source));
    });
}

void ImportWizardViewModel::applySource(services::ImportSource source) {
    // A CSV without a table (an open quote) has nothing to preview: it stays on the file step.
    if (!source.tableStatus && source.kind == services::ImportFileKind::Csv) {
        failWith(source.tableStatus.error(), source.errorLine);
        return;
    }
    m_source = std::move(source);
    m_previous.clear();
    if (auto previous = m_imports.previousImports(*m_source, m_kind)) {
        m_previous = std::move(*previous);
    } else {
        qWarning("ImportWizardViewModel: %s", previous.error().detail.c_str());
    }
    m_squadMapping = {};
    m_mappingRemembered = false;
    m_activityMapping = {};
    m_options = {};
    m_options.updateExisting = m_updateExisting;
    m_options.skipInvalid = m_skipInvalid;
    // "—", as for the squad (P19 decision 7): m_options starts without a default position.
    if (m_source->tableStatus && activity()) {
        if (auto metrics = m_imports.metricsFor(m_kind)) {
            m_metrics = std::move(*metrics);
        } else {
            qWarning("ImportWizardViewModel: %s", metrics.error().detail.c_str());
            m_metrics.clear();
        }
        if (auto suggestion = m_imports.suggestActivityMapping(m_kind, *m_source)) {
            m_activityMapping = std::move(suggestion->mapping);
            m_mappingRemembered = suggestion->remembered;
        } else {
            qWarning("ImportWizardViewModel: %s", suggestion.error().detail.c_str());
        }
        const auto& headers = m_source->table.headers;
        for (std::size_t column = 0; column < m_activityMapping.columns.size(); ++column) {
            if (m_activityMapping.columns[column].field == playerField(m_kind)) {
                m_options.nameOrder = core::detectNameOrder(headers[column]);
            }
        }
    } else if (m_source->tableStatus) {
        if (auto suggestion = m_imports.suggestSquadMapping(*m_source)) {
            m_squadMapping = std::move(suggestion->mapping);
            m_mappingRemembered = suggestion->remembered;
        } else {
            qWarning("ImportWizardViewModel: %s", suggestion.error().detail.c_str());
            m_squadMapping = core::autoMapSquad(m_source->table.headers);
        }
        // The default is "—", a player without a position (P19 decision 7), unless a remembered
        // mapping keeps a position of its own.
    } else {
        // The file is fine, the sheet is not: the preview offers the other sheets.
        failWith(m_source->tableStatus.error());
    }
    clearReport();
    rebuildMappingRows();
    emit sourceChanged();
    emit mappingChanged();
    setStep(kPreviewStep);
}

void ImportWizardViewModel::setStep(const QString& step) {
    if (step == m_step) {
        return;
    }
    m_step = step;
    emit stepChanged();
    emit reportChanged(); // canCommit depends on the step
}

void ImportWizardViewModel::setBusy(bool busy, double progress) {
    const bool changed = busy != m_busy;
    m_busy = busy;
    m_progress = progress;
    emit progressChanged();
    if (changed) {
        emit busyChanged();
        emit reportChanged();
    }
}

void ImportWizardViewModel::setErrorText(const QString& text) {
    if (text == m_errorText) {
        return;
    }
    m_errorText = text;
    emit errorTextChanged();
}

void ImportWizardViewModel::failWith(const core::Error& error, std::optional<int> line) {
    qInfo("ImportWizardViewModel: %s", error.detail.c_str());
    m_error = error;
    m_errorLine = line;
    setErrorText(errorMessage());
}

QString ImportWizardViewModel::errorMessage() const {
    if (!m_error) {
        return {};
    }
    if (m_error->field == "import_csv_quote" && m_errorLine) {
        return tr("The CSV has a quoted field that is never closed, starting on line %1. Close the "
                  "quote in the file and import it again.")
            .arg(*m_errorLine);
    }
    return ErrorPresenter::message(*m_error);
}

void ImportWizardViewModel::rebuildMappingRows() {
    QList<QVariantList> rows;
    if (m_source && m_source->tableStatus && activity()) {
        const auto& table = m_source->table;
        const core::ActivityMapping automatic =
            m_kind == services::ImportKind::Matches
                ? core::autoMapMatches(table.headers, m_metrics)
                : core::autoMapTraining(table.headers, m_metrics);
        for (std::size_t column = 0; column < table.headers.size(); ++column) {
            QStringList samples;
            for (const core::SourceRow& row : table.rows) {
                if (samples.size() >= kSamples) {
                    break;
                }
                const QString value = fromStd(core::cellAt(row, column)).trimmed();
                if (!value.isEmpty()) {
                    samples.append(value);
                }
            }
            const core::ColumnTarget target = column < m_activityMapping.columns.size()
                                                  ? m_activityMapping.columns[column]
                                                  : core::ColumnTarget{};
            const bool mapped = target.field || target.metric;
            rows.append(QVariantList{static_cast<int>(column), columnTitle(table.headers, column),
                                     samples.join(QStringLiteral(", ")),
                                     choiceOf(m_kind, target, m_metrics),
                                     mapped && automatic.columns[column] == target});
        }
    } else if (m_source && m_source->tableStatus) {
        const auto& table = m_source->table;
        const core::SquadMapping automatic = core::autoMapSquad(table.headers);
        for (std::size_t column = 0; column < table.headers.size(); ++column) {
            QStringList samples;
            for (const core::SourceRow& row : table.rows) {
                if (samples.size() >= kSamples) {
                    break;
                }
                const QString value = fromStd(core::cellAt(row, column)).trimmed();
                if (!value.isEmpty()) {
                    samples.append(value);
                }
            }
            const auto field = column < m_squadMapping.columns.size()
                                   ? m_squadMapping.columns[column]
                                   : std::nullopt;
            const bool autoMatched = field && automatic.columns[column] == field;
            rows.append(QVariantList{static_cast<int>(column), columnTitle(table.headers, column),
                                     samples.join(QStringLiteral(", ")),
                                     field ? QString::fromUtf8(core::toKey(*field)) : QString(),
                                     autoMatched});
        }
    }
    m_mapping->setRows(std::move(rows));
}

void ImportWizardViewModel::rebuildActivityReport(QList<QVariantList>& rows) {
    m_activityRows.clear();
    if (m_trainingImport && m_trainingPlan) {
        m_activityRows = trainingReport(*m_trainingImport, *m_trainingPlan, m_metrics);
    } else if (m_matchImport && m_matchPlan) {
        m_activityRows = matchReport(*m_matchImport, *m_matchPlan, m_metrics);
    }
    for (const ActivityReportRow& row : m_activityRows) {
        rows.append(QVariantList{row.rowNumber, ImportTexts::statusKey(row.status),
                                 ImportTexts::statusName(row.status), row.name, row.action,
                                 row.details, row.message});
    }
}

void ImportWizardViewModel::rebuildReport() {
    QList<QVariantList> rows;
    if (activity()) {
        rebuildActivityReport(rows);
        m_report->setRows(std::move(rows));
        emit reportChanged();
        return;
    }
    const int year = currentYear();
    const core::ExistingPolicy policy = existingPolicy();
    for (const core::SquadRow& row : m_rows) {
        const core::RowStatus status = core::statusFor(row, policy);
        QString name = fromStd(row.draft.firstName);
        if (!row.draft.lastName.empty()) {
            name +=
                (name.isEmpty() ? QString() : QStringLiteral(" ")) + fromStd(row.draft.lastName);
        }
        rows.append(QVariantList{
            row.sourceRow, ImportTexts::statusKey(status), ImportTexts::statusName(status), name,
            ImportTexts::actionName(core::actionFor(row, policy)), ImportTexts::details(row, year),
            ImportTexts::rowMessage(row, year)});
    }
    m_report->setRows(std::move(rows));
    emit reportChanged();
}

void ImportWizardViewModel::clearReport() {
    m_trainingImport.reset();
    m_trainingPlan.reset();
    m_matchImport.reset();
    m_matchPlan.reset();
    m_activityRows.clear();
    if (m_rows.empty() && m_report->rowCount() == 0) {
        return;
    }
    m_rows.clear();
    m_report->setRows({});
    emit reportChanged();
}

void ImportWizardViewModel::invalidateValidation() {
    ++m_request;
    if (m_validation) {
        m_validation->cancel();
        disconnect(m_validation, nullptr, this, nullptr);
        m_validation->deleteLater();
        m_validation = nullptr;
    }
    setBusy(false);
    clearReport();
    if (m_step == kReportStep) {
        setStep(kMappingStep);
    }
}

int ImportWizardViewModel::currentYear() const {
    return static_cast<int>(ServiceRegistry::instance().clock().today().year());
}

core::ExistingPolicy ImportWizardViewModel::existingPolicy() const {
    return m_updateExisting ? core::ExistingPolicy::Update : core::ExistingPolicy::Skip;
}

QString ImportWizardViewModel::previousImportText() const {
    if (m_previous.empty()) {
        return {};
    }
    const services::PreviousImport& newest = m_previous.front();
    const QString date = IsoDates::displayText(localDate(newest.timestamp));
    if (newest.undone) {
        return tr("This file was already imported on %1; that import was undone.").arg(date);
    }
    if (activity()) {
        return tr("This file was already imported on %1. Check the report: rows already in the app "
                  "show \"No change\" and are not added twice.")
            .arg(date);
    }
    return tr("This file was already imported on %1. Check the report: players already in the "
              "squad are skipped or updated, not added twice.")
        .arg(date);
}

} // namespace fdss::viewmodels
