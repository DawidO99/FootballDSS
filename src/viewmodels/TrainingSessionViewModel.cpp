#include "viewmodels/TrainingSessionViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"
#include "services/TrainingService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCollator>
#include <QCoreApplication>
#include <QEvent>
#include <QLocale>
#include <QVariantMap>

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace fdss::viewmodels {

namespace {

const QList<int> kEditedRoles{
    TrainingSessionViewModel::StatusRole,         TrainingSessionViewModel::RpeRole,
    TrainingSessionViewModel::MinutesRole,        TrainingSessionViewModel::LoadRole,
    TrainingSessionViewModel::CellsEnabledRole,   TrainingSessionViewModel::RowErrorRole,
    TrainingSessionViewModel::MinutesHasErrorRole};

QVariant optionalInt(const std::optional<int>& value) {
    return value ? QVariant(*value) : QVariant{};
}

QString statusName(core::AttendanceStatus status) {
    switch (status) {
    case core::AttendanceStatus::Present:
        return TrainingSessionViewModel::tr("Present");
    case core::AttendanceStatus::Absent:
        return TrainingSessionViewModel::tr("Absent");
    case core::AttendanceStatus::Excused:
        return TrainingSessionViewModel::tr("Excused");
    case core::AttendanceStatus::Injured:
        return TrainingSessionViewModel::tr("Injured");
    }
    return {};
}

// An override equal to the duration means no override, so the row follows a later change of the
// duration, and typing the same number again is no change.
void normalize(core::AttendanceDraft& draft, int durationMin) {
    if (draft.minutesOverride == durationMin) {
        draft.minutesOverride.reset();
    }
}

} // namespace

TrainingSessionViewModel::TrainingSessionViewModel(QObject* parent)
    : QAbstractListModel(parent), m_training(ServiceRegistry::instance().training()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()), m_sessions(new SessionPickerModel(this)) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    m_digitTimer.start();
}

int TrainingSessionViewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TrainingSessionViewModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case PlayerIdRole:
        return QVariant::fromValue<qint64>(row.playerId.value);
    case ShirtNumberRole:
        return optionalInt(row.shirtNumber);
    case PlayerNameRole:
        return row.name;
    case StatusRole:
        return QString::fromUtf8(core::toDbText(row.draft.status));
    case RpeRole:
        return optionalInt(row.draft.rpe);
    case MinutesRole:
        return optionalInt(minutesOf(row));
    case LoadRole: {
        const std::optional<int> minutes = minutesOf(row);
        if (!row.draft.rpe || !minutes || minutesHaveError(row)) {
            return {};
        }
        return *row.draft.rpe * *minutes;
    }
    case CellsEnabledRole:
        return !readOnly() && row.draft.status == core::AttendanceStatus::Present;
    case RowErrorRole:
        return rowError(row);
    case MinutesHasErrorRole:
        return minutesHaveError(row);
    default:
        return {};
    }
}

QHash<int, QByteArray> TrainingSessionViewModel::roleNames() const {
    return {{PlayerIdRole, "playerId"},
            {ShirtNumberRole, "shirtNumber"},
            {PlayerNameRole, "playerName"},
            {StatusRole, "status"},
            {RpeRole, "rpe"},
            {MinutesRole, "minutes"},
            {LoadRole, "load"},
            {CellsEnabledRole, "cellsEnabled"},
            {RowErrorRole, "rowError"},
            {MinutesHasErrorRole, "minutesHasError"}};
}

qint64 TrainingSessionViewModel::teamId() const { return m_teamId; }

void TrainingSessionViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    reload();
}

SessionPickerModel* TrainingSessionViewModel::sessions() const { return m_sessions; }

qint64 TrainingSessionViewModel::sessionId() const { return m_sessionId; }

int TrainingSessionViewModel::durationMin() const { return m_durationMin; }

bool TrainingSessionViewModel::hasSessions() const { return m_sessions->rowCount() > 0; }

int TrainingSessionViewModel::count() const { return rowCount(); }

bool TrainingSessionViewModel::readOnly() const {
    return !m_session.isAllowed(core::Permission::EditTraining);
}

bool TrainingSessionViewModel::dirty() const { return m_dirty; }

bool TrainingSessionViewModel::canSave() const { return m_canSave; }

int TrainingSessionViewModel::unsavedCount() const { return m_unsavedCount; }

QString TrainingSessionViewModel::unsavedText() const {
    if (m_unsavedCount == 0) {
        return {};
    }
    return m_unsavedCount == 1 ? tr("Not saved: 1 player")
                               : tr("Not saved: %1 players").arg(m_unsavedCount);
}

int TrainingSessionViewModel::maxRpe() const { return core::kMaxRpe; }

int TrainingSessionViewModel::maxMinutes() const { return core::kMaxAttendanceMinutes; }

QVariantList TrainingSessionViewModel::statusOptions() const {
    QVariantList options;
    for (const core::AttendanceStatus status : core::allValues<core::AttendanceStatus>()) {
        options.append(
            QVariantMap{{QStringLiteral("value"), QString::fromUtf8(core::toDbText(status))},
                        {QStringLiteral("text"), statusName(status)}});
    }
    return options;
}

QString TrainingSessionViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void TrainingSessionViewModel::reload() {
    setLoadFailure(std::nullopt);
    refreshSessions();
    // The rows of the user stay; a dirty sheet is only reloaded by leaving it, which asks first.
    if (m_dirty) {
        return;
    }
    if (m_sessionId != 0 && m_sessions->contains(m_sessionId)) {
        core::Error failure;
        if (!loadSheet(m_sessionId, failure)) {
            qCritical("TrainingSessionViewModel: %s", failure.detail.c_str());
            setLoadFailure(failure);
        }
        return;
    }
    openDefaultSession();
}

void TrainingSessionViewModel::openSession(qint64 sessionId) {
    core::Error failure;
    if (loadSheet(sessionId, failure)) {
        setLoadFailure(std::nullopt);
        return;
    }
    if (failure.code != core::ErrorCode::NotFound) {
        qCritical("TrainingSessionViewModel: %s", failure.detail.c_str());
        setLoadFailure(failure);
        return;
    }
    // Deleted in the calendar, for example, or an old link.
    emit errorOccurred(ErrorPresenter::message(failure));
    refreshSessions();
    openDefaultSession();
}

void TrainingSessionViewModel::openDefaultSession() {
    const qint64 id = m_sessions->defaultSessionId(m_clock.today());
    if (id == 0) {
        clearSheet();
        return;
    }
    core::Error failure;
    if (!loadSheet(id, failure)) {
        qCritical("TrainingSessionViewModel: %s", failure.detail.c_str());
        clearSheet();
        setLoadFailure(failure);
    }
}

void TrainingSessionViewModel::setStatus(int row, const QString& status) {
    if (row < 0 || row >= rowCount() || readOnly()) {
        return;
    }
    const auto parsed = core::fromDbText<core::AttendanceStatus>(status.toStdString());
    if (!parsed) {
        qWarning("TrainingSessionViewModel: unknown status '%s'", qUtf8Printable(status));
        return;
    }
    core::AttendanceDraft& draft = m_rows[static_cast<std::size_t>(row)].draft;
    if (draft.status == *parsed) {
        return;
    }
    draft.status = *parsed;
    if (*parsed != core::AttendanceStatus::Present) {
        draft.rpe.reset();
        draft.minutesOverride.reset();
    }
    m_lastOne.reset();
    afterRowEdit(row);
}

void TrainingSessionViewModel::enterRpeDigit(int row, int digit) {
    enterRpeDigitAt(row, digit, m_digitTimer.elapsed());
}

void TrainingSessionViewModel::enterRpeDigitAt(int row, int digit, qint64 elapsedMs) {
    if (digit < 0 || digit > 9 || !isEditable(row)) {
        return;
    }
    const bool makesTen = digit == 0 && m_lastOne && m_lastOne->row == row &&
                          elapsedMs - m_lastOne->elapsedMs <= kTenWindowMs;
    m_lastOne = digit == 1 ? std::optional(LastOne{row, elapsedMs}) : std::nullopt;
    setRpe(row, makesTen ? core::kMaxRpe : digit);
}

void TrainingSessionViewModel::clearRpe(int row) {
    if (!isEditable(row)) {
        return;
    }
    m_lastOne.reset();
    setRpe(row, std::nullopt);
}

void TrainingSessionViewModel::setMinutesText(int row, const QString& text) {
    if (!isEditable(row)) {
        return;
    }
    std::optional<int> minutes;
    if (!text.isEmpty()) {
        bool ok = false;
        const int value = text.toInt(&ok);
        if (!ok) {
            return;
        }
        minutes = value;
    }
    core::AttendanceDraft& draft = m_rows[static_cast<std::size_t>(row)].draft;
    draft.minutesOverride = minutes;
    normalize(draft, m_durationMin);
    afterRowEdit(row);
}

void TrainingSessionViewModel::rememberCell(int row, int column) {
    m_lastOne.reset();
    if (row < 0 || row >= rowCount()) {
        m_remembered.reset();
        return;
    }
    m_remembered = RememberedCell{row, column, m_rows[static_cast<std::size_t>(row)].draft};
}

void TrainingSessionViewModel::revertCell(int row, int column) {
    m_lastOne.reset();
    if (readOnly() || !m_remembered || m_remembered->row != row || m_remembered->column != column) {
        return;
    }
    core::AttendanceDraft& draft = m_rows[static_cast<std::size_t>(row)].draft;
    switch (column) {
    case StatusColumn:
        draft = m_remembered->draft;
        break;
    case RpeColumn:
        draft.rpe = m_remembered->draft.rpe;
        break;
    case MinutesColumn:
        draft.minutesOverride = m_remembered->draft.minutesOverride;
        break;
    default:
        return;
    }
    afterRowEdit(row);
}

void TrainingSessionViewModel::markAllPresent() {
    if (readOnly()) {
        return;
    }
    m_lastOne.reset();
    for (int row = 0; row < rowCount(); ++row) {
        Row& current = m_rows[static_cast<std::size_t>(row)];
        if (current.defaultStatus == core::AttendanceStatus::Present &&
            current.draft.status != core::AttendanceStatus::Present) {
            current.draft.status = core::AttendanceStatus::Present;
            afterRowEdit(row);
        }
    }
}

bool TrainingSessionViewModel::save() {
    if (!checkAllowed() || m_sessionId == 0) {
        return false;
    }
    std::vector<core::AttendanceDraft> drafts;
    drafts.reserve(m_rows.size());
    for (const Row& row : m_rows) {
        drafts.push_back(row.draft);
    }
    const auto result = m_training.saveAttendance(core::TrainingSessionId{m_sessionId}, drafts);
    if (!result) {
        report(result.error());
        return false;
    }
    for (Row& row : m_rows) {
        row.saveError.reset();
    }
    if (!result->empty()) {
        for (const services::AttendanceRowError& error : *result) {
            const auto found = std::ranges::find_if(
                m_rows, [&](const Row& row) { return row.playerId == error.player; });
            if (found != m_rows.end()) {
                found->saveError = error.error;
            }
        }
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1), kEditedRoles);
        }
        emit errorOccurred(tr("Nothing was saved. Correct the rows marked with an error."));
        return false;
    }
    m_baseline = std::move(drafts);
    for (Row& row : m_rows) {
        row.saved = true;
    }
    // Esc after a save must not bring back what came before it.
    if (m_remembered && m_remembered->row < rowCount()) {
        m_remembered->draft = m_rows[static_cast<std::size_t>(m_remembered->row)].draft;
    }
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), kEditedRoles);
    }
    updateDirty();
    emit saved();
    return true;
}

void TrainingSessionViewModel::discardChanges() {
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        m_rows[row].draft = m_baseline[row];
        m_rows[row].saveError.reset();
    }
    m_remembered.reset();
    m_lastOne.reset();
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), kEditedRoles);
    }
    updateDirty();
}

int TrainingSessionViewModel::firstErrorRow() const {
    const auto found =
        std::ranges::find_if(m_rows, [this](const Row& row) { return !rowError(row).isEmpty(); });
    return found != m_rows.end() ? static_cast<int>(std::distance(m_rows.begin(), found)) : -1;
}

bool TrainingSessionViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        emit optionsChanged();
        emit loadErrorChanged();
        emit unsavedChanged();
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1), {RowErrorRole});
        }
    }
    return QAbstractListModel::eventFilter(watched, event);
}

bool TrainingSessionViewModel::isEditable(int row) const {
    return row >= 0 && row < rowCount() && !readOnly() &&
           m_rows[static_cast<std::size_t>(row)].draft.status == core::AttendanceStatus::Present;
}

bool TrainingSessionViewModel::loadSheet(qint64 sessionId, core::Error& failure) {
    auto sheet = m_training.sheet(core::TrainingSessionId{sessionId});
    if (!sheet) {
        failure = sheet.error();
        return false;
    }
    applySheet(std::move(*sheet));
    return true;
}

void TrainingSessionViewModel::applySheet(services::AttendanceSheet sheet) {
    // The names are Polish whatever the language of the app.
    QCollator collator(QLocale(QLocale::Polish, QLocale::Poland));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::stable_sort(sheet.rows, [&](const services::AttendanceSheetRow& a,
                                             const services::AttendanceSheetRow& b) {
        constexpr int kNoNumber = std::numeric_limits<int>::max();
        const int numberA = a.player.shirtNumber.value_or(kNoNumber);
        const int numberB = b.player.shirtNumber.value_or(kNoNumber);
        if (numberA != numberB) {
            return numberA < numberB;
        }
        const int surnames = collator.compare(QString::fromStdString(a.player.lastName),
                                              QString::fromStdString(b.player.lastName));
        if (surnames != 0) {
            return surnames < 0;
        }
        return collator.compare(QString::fromStdString(a.player.firstName),
                                QString::fromStdString(b.player.firstName)) < 0;
    });

    std::vector<Row> rows;
    std::vector<core::AttendanceDraft> baseline;
    rows.reserve(sheet.rows.size());
    baseline.reserve(sheet.rows.size());
    for (services::AttendanceSheetRow& source : sheet.rows) {
        normalize(source.draft, sheet.session.durationMin);
        baseline.push_back(source.draft);
        Row row;
        row.playerId = source.player.id;
        row.shirtNumber = source.player.shirtNumber;
        row.name = SquadTexts::displayName(source.player);
        row.draft = source.draft;
        row.defaultStatus = source.defaultStatus;
        row.saved = source.saved;
        rows.push_back(std::move(row));
    }

    // Everything the rows read comes first: a reset creates the delegates while it is emitted, and
    // they read the model right then.
    m_baseline = std::move(baseline);
    m_sessionId = sheet.session.id.value;
    m_durationMin = sheet.session.durationMin;
    m_remembered.reset();
    m_lastOne.reset();
    const bool samePlayers = std::ranges::equal(
        rows, m_rows, [](const Row& a, const Row& b) { return a.playerId == b.playerId; });
    if (samePlayers) {
        m_rows = std::move(rows);
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1));
        }
    } else {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    emit sessionChanged();
    updateDirty();
}

void TrainingSessionViewModel::clearSheet() {
    m_baseline.clear();
    m_sessionId = 0;
    m_durationMin = 0;
    m_remembered.reset();
    m_lastOne.reset();
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit sessionChanged();
    updateDirty();
}

void TrainingSessionViewModel::refreshSessions() {
    if (m_teamId == 0 || !m_session.user()) {
        m_sessions->setSessions({});
        emit sessionsChanged();
        return;
    }
    auto sessions = m_training.listSessions(core::TeamId{m_teamId});
    if (!sessions) {
        qCritical("TrainingSessionViewModel: %s", sessions.error().detail.c_str());
        setLoadFailure(sessions.error());
        return;
    }
    m_sessions->setSessions(std::move(*sessions));
    emit sessionsChanged();
}

void TrainingSessionViewModel::setRpe(int row, std::optional<int> rpe) {
    m_rows[static_cast<std::size_t>(row)].draft.rpe = rpe;
    afterRowEdit(row);
}

void TrainingSessionViewModel::afterRowEdit(int row) {
    m_rows[static_cast<std::size_t>(row)].saveError.reset();
    emit dataChanged(index(row), index(row), kEditedRoles);
    updateDirty();
}

void TrainingSessionViewModel::updateDirty() {
    int unsaved = 0;
    bool dirty = false;
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        const bool changed = m_rows[row].draft != m_baseline[row];
        dirty = dirty || changed;
        if (changed || !m_rows[row].saved) {
            ++unsaved;
        }
    }
    const bool canSave = !readOnly() && unsaved > 0;
    if (unsaved != m_unsavedCount) {
        m_unsavedCount = unsaved;
        emit unsavedChanged();
    }
    if (dirty != m_dirty || canSave != m_canSave) {
        m_dirty = dirty;
        m_canSave = canSave;
        emit dirtyChanged();
    }
}

void TrainingSessionViewModel::setLoadFailure(std::optional<core::Error> failure) {
    const bool changed = failure.has_value() || m_loadFailure.has_value();
    m_loadFailure = std::move(failure);
    if (changed) {
        emit loadErrorChanged();
    }
}

QString TrainingSessionViewModel::rowError(const Row& row) const {
    if (row.saveError) {
        return ErrorPresenter::message(*row.saveError);
    }
    const core::ValidationErrors errors = core::validate(row.draft);
    if (errors.empty()) {
        return {};
    }
    return ErrorPresenter::message(core::Error{errors.front().code, {}, errors.front().field});
}

bool TrainingSessionViewModel::minutesHaveError(const Row& row) const {
    if (row.saveError && row.saveError->field == "minutes_override") {
        return true;
    }
    return std::ranges::any_of(core::validate(row.draft), [](const core::ValidationError& error) {
        return error.field == "minutes_override";
    });
}

std::optional<int> TrainingSessionViewModel::minutesOf(const Row& row) const {
    if (row.draft.status != core::AttendanceStatus::Present) {
        return std::nullopt;
    }
    return row.draft.minutesOverride.value_or(m_durationMin);
}

bool TrainingSessionViewModel::checkAllowed() {
    if (m_session.isAllowed(core::Permission::EditTraining)) {
        return true;
    }
    report(core::Error{
        core::ErrorCode::PermissionDenied, "refused by the viewmodel: EditTraining is needed", {}});
    return false;
}

void TrainingSessionViewModel::report(const core::Error& error) {
    qCritical("TrainingSessionViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
