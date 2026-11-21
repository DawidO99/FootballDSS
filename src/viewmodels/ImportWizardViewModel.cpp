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
         tr("Players with their names, shirt numbers, birth years, positions and foot.")}}};
}

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

QVariantList ImportWizardViewModel::fieldOptions() const { return ImportTexts::fieldOptions(); }

bool ImportWizardViewModel::mappingRemembered() const { return m_mappingRemembered; }

bool ImportWizardViewModel::nameOrderVisible() const {
    return columnOf(m_squadMapping, core::SquadField::FullName).has_value();
}

QString ImportWizardViewModel::nameOrder() const {
    return QString::fromUtf8(core::toKey(m_squadMapping.nameOrder));
}

QVariantList ImportWizardViewModel::nameOrderOptions() const {
    return ImportTexts::nameOrderOptions();
}

QVariantList ImportWizardViewModel::nameSplitPreview() const {
    QVariantList rows;
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
    return m_source && m_source->tableStatus &&
           core::needsDefaultPosition(m_squadMapping, m_source->table);
}

QString ImportWizardViewModel::defaultPosition() const {
    return m_squadMapping.defaultPosition
               ? QString::fromUtf8(core::toDbText(*m_squadMapping.defaultPosition))
               : QString();
}

QVariantList ImportWizardViewModel::positionOptions() const {
    return SquadTexts::positionOptions();
}

QStringList ImportWizardViewModel::mappingErrors() const {
    QStringList errors;
    if (!m_source || !m_source->tableStatus) {
        return errors;
    }
    for (const core::MappingIssue issue : core::checkMapping(m_squadMapping, m_source->table)) {
        errors.append(ImportTexts::mappingIssue(issue));
    }
    return errors;
}

RowsModel* ImportWizardViewModel::report() const { return m_report; }

namespace {

int countRows(const std::vector<core::SquadRow>& rows, core::RowStatus status) {
    return static_cast<int>(std::ranges::count_if(
        rows, [status](const core::SquadRow& row) { return row.status() == status; }));
}

int countActions(const std::vector<core::SquadRow>& rows, core::ExistingPolicy policy,
                 std::initializer_list<core::RowAction> actions) {
    return static_cast<int>(std::ranges::count_if(rows, [&](const core::SquadRow& row) {
        return std::ranges::find(actions, core::actionFor(row, policy)) != actions.end();
    }));
}

} // namespace

int ImportWizardViewModel::okCount() const { return countRows(m_rows, core::RowStatus::Ok); }

int ImportWizardViewModel::warningCount() const {
    return countRows(m_rows, core::RowStatus::Warning);
}

int ImportWizardViewModel::errorCount() const { return countRows(m_rows, core::RowStatus::Error); }

int ImportWizardViewModel::createCount() const {
    return countActions(m_rows, existingPolicy(), {core::RowAction::Create});
}

int ImportWizardViewModel::updateCount() const {
    return countActions(m_rows, existingPolicy(), {core::RowAction::Update});
}

int ImportWizardViewModel::skipCount() const {
    return countActions(m_rows, existingPolicy(),
                        {core::RowAction::SkipExisting, core::RowAction::SkipAmbiguous});
}

bool ImportWizardViewModel::updateExisting() const { return m_updateExisting; }

void ImportWizardViewModel::setUpdateExisting(bool update) {
    if (update == m_updateExisting) {
        return;
    }
    m_updateExisting = update;
    if (m_busy && m_validation) {
        invalidateValidation();
    }
    rebuildReport();
}

bool ImportWizardViewModel::skipInvalid() const { return m_skipInvalid; }

void ImportWizardViewModel::setSkipInvalid(bool skip) {
    if (skip == m_skipInvalid) {
        return;
    }
    m_skipInvalid = skip;
    if (m_busy && m_validation) {
        invalidateValidation();
    }
    emit reportChanged();
}

bool ImportWizardViewModel::canCommit() const {
    return !m_busy && m_step == kReportStep && m_source && m_source->tableStatus && canImport() &&
           createCount() + updateCount() > 0 && (errorCount() == 0 || m_skipInvalid);
}

QVariantMap ImportWizardViewModel::summary() const {
    QVariantMap values;
    if (!m_summary) {
        return values;
    }
    values.insert(QStringLiteral("created"), m_summary->created);
    values.insert(QStringLiteral("updated"), m_summary->updated);
    values.insert(QStringLiteral("skipped"),
                  m_summary->skippedExisting + m_summary->skippedAmbiguous);
    values.insert(QStringLiteral("invalid"), m_summary->invalid);
    QStringList changedMatches;
    for (const core::SquadRow& row : m_rows) {
        if (std::ranges::any_of(row.issues, [](const core::RowIssue& issue) {
                return issue.kind == core::RowIssueKind::MatchChanged;
            })) {
            changedMatches.append(ImportTexts::rowMessage(row, currentYear()) + QLatin1Char(' ') +
                                  ImportTexts::actionName(core::actionFor(row, existingPolicy())));
        }
    }
    values.insert(QStringLiteral("changedMatches"), changedMatches.join(QLatin1Char('\n')));
    if (m_undo) {
        values.insert(QStringLiteral("deleted"), m_undo->deleted);
        values.insert(QStringLiteral("archived"), m_undo->archived);
        values.insert(QStringLiteral("notReverted"), m_undo->notReverted);
    }
    return values;
}

bool ImportWizardViewModel::canUndo() const {
    return m_summary.has_value() && !m_undo.has_value() && canImport();
}

bool ImportWizardViewModel::undone() const { return m_undo.has_value(); }

void ImportWizardViewModel::chooseType(const QString& type) {
    if (type != kSquadType || !canImport()) {
        return;
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

void ImportWizardViewModel::setNameOrder(const QString& order) {
    const auto value = core::nameOrderFromKey(order.toStdString());
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
    auto future = m_imports.validateSquad(core::TeamId{m_teamId}, *m_source, m_squadMapping);
    if (!future) {
        emit errorOccurred(ErrorPresenter::message(future.error()));
        return;
    }
    setBusy(true, 0);
    delete m_validation;
    m_validation = new QFutureWatcher<std::vector<core::SquadRow>>(this);
    auto* watcher = m_validation;
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this,
            [this, watcher, request](int value) {
                if (request != m_request) {
                    return;
                }
                const int maximum = watcher->progressMaximum();
                m_progress = maximum > 0 ? static_cast<double>(value) / maximum : 0;
                emit progressChanged();
            });
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

void ImportWizardViewModel::commit() {
    if (!canCommit()) {
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
    auto result = m_imports.undo(m_summary->importId);
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
        setStep(kFileStep);
    } else if (m_step == kFileStep) {
        setErrorText({});
        m_error.reset();
        setStep(kTypeStep);
    }
}

void ImportWizardViewModel::reset() {
    invalidateValidation();
    setErrorText({});
    m_error.reset();
    m_source.reset();
    m_previous.clear();
    m_squadMapping = {};
    m_mappingRemembered = false;
    m_updateExisting = false;
    m_skipInvalid = true;
    m_summary.reset();
    m_undo.reset();
    clearReport();
    rebuildMappingRows();
    emit sourceChanged();
    emit mappingChanged();
    emit summaryChanged();
    setStep(kTypeStep);
}

bool ImportWizardViewModel::saveTemplate(const QUrl& target, const QString& format) {
    const QString source = format == QLatin1StringView("xlsx")
                               ? QStringLiteral(":/fdss/import-templates/squad.xlsx")
                               : QStringLiteral(":/fdss/import-templates/squad.csv");
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
            setErrorText(ErrorPresenter::message(*m_error));
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
    auto future = m_imports.read(path, sheet);
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
    m_source = std::move(source);
    m_previous.clear();
    if (auto previous = m_imports.previousImports(*m_source)) {
        m_previous = std::move(*previous);
    } else {
        qWarning("ImportWizardViewModel: %s", previous.error().detail.c_str());
    }
    m_squadMapping = {};
    m_mappingRemembered = false;
    if (m_source->tableStatus) {
        if (auto suggestion = m_imports.suggestSquadMapping(*m_source)) {
            m_squadMapping = std::move(suggestion->mapping);
            m_mappingRemembered = suggestion->remembered;
        } else {
            qWarning("ImportWizardViewModel: %s", suggestion.error().detail.c_str());
            m_squadMapping = core::autoMapSquad(m_source->table.headers);
        }
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

void ImportWizardViewModel::failWith(const core::Error& error) {
    qInfo("ImportWizardViewModel: %s", error.detail.c_str());
    m_error = error;
    setErrorText(ErrorPresenter::message(error));
}

void ImportWizardViewModel::rebuildMappingRows() {
    QList<QVariantList> rows;
    if (m_source && m_source->tableStatus) {
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

void ImportWizardViewModel::rebuildReport() {
    QList<QVariantList> rows;
    const int year = currentYear();
    const core::ExistingPolicy policy = existingPolicy();
    for (const core::SquadRow& row : m_rows) {
        const core::RowStatus status = row.status();
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
    return tr("This file was already imported on %1. Check the report: players already in the "
              "squad are skipped or updated, not added twice.")
        .arg(date);
}

} // namespace fdss::viewmodels
