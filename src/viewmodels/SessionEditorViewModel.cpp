#include "viewmodels/SessionEditorViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "services/CalendarService.hpp"
#include "services/Session.hpp"
#include "viewmodels/CalendarTexts.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <string_view>
#include <utility>

namespace fdss::viewmodels {

namespace {

constexpr std::string_view kDate = "date";
constexpr std::string_view kStartTime = "start_time";
constexpr std::string_view kDuration = "duration_min";
constexpr int kDurationStep = 15;

bool isEditorField(std::string_view field) {
    return field == kDate || field == kStartTime || field == kDuration;
}

// JavaScript sends null, undefined or a number, whose exact type does not matter here.
QVariant numberOrNull(const QVariant& value) {
    return value.isValid() && !value.isNull() ? QVariant(value.toInt()) : QVariant{};
}

} // namespace

SessionEditorViewModel::SessionEditorViewModel(QObject* parent)
    : QObject(parent), m_calendar(ServiceRegistry::instance().calendar()),
      m_session(ServiceRegistry::instance().session()) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 SessionEditorViewModel::teamId() const { return m_teamId; }

void SessionEditorViewModel::setTeamId(qint64 teamId) {
    if (teamId != m_teamId) {
        m_teamId = teamId;
        emit teamIdChanged();
    }
}

bool SessionEditorViewModel::adding() const { return m_sessionId == 0; }

bool SessionEditorViewModel::readOnly() const {
    return !m_session.isAllowed(core::Permission::EditCalendar);
}

qint64 SessionEditorViewModel::sessionId() const { return m_sessionId; }

bool SessionEditorViewModel::dirty() const { return m_fields != m_loaded; }

QString SessionEditorViewModel::date() const { return m_fields.date; }

void SessionEditorViewModel::setDate(const QString& date) {
    if (date != m_fields.date) {
        m_fields.date = date;
        clearError(std::string(kDate));
        emit fieldsChanged();
        emit dirtyChanged();
    }
}

QString SessionEditorViewModel::startTime() const { return m_fields.startTime; }

void SessionEditorViewModel::setStartTime(const QString& time) {
    if (time != m_fields.startTime) {
        m_fields.startTime = time;
        clearError(std::string(kStartTime));
        emit fieldsChanged();
        emit dirtyChanged();
    }
}

QVariant SessionEditorViewModel::durationMin() const { return m_fields.durationMin; }

void SessionEditorViewModel::setDurationMin(const QVariant& minutes) {
    const QVariant value = numberOrNull(minutes);
    if (value != m_fields.durationMin) {
        m_fields.durationMin = value;
        clearError(std::string(kDuration));
        emit durationMinChanged();
        emit dirtyChanged();
    }
}

QString SessionEditorViewModel::type() const {
    return QString::fromUtf8(core::toDbText(m_fields.type));
}

void SessionEditorViewModel::setType(const QString& type) {
    const auto parsed = core::fromDbText<core::SessionType>(type.toStdString());
    if (!parsed) {
        report(parsed.error());
        return;
    }
    if (*parsed != m_fields.type) {
        m_fields.type = *parsed;
        emit fieldsChanged();
        emit dirtyChanged();
    }
}

QString SessionEditorViewModel::note() const { return m_fields.note; }

void SessionEditorViewModel::setNote(const QString& note) {
    if (note != m_fields.note) {
        m_fields.note = note;
        emit fieldsChanged();
        emit dirtyChanged();
    }
}

int SessionEditorViewModel::minDuration() const { return core::kMinSessionDuration; }

int SessionEditorViewModel::maxDuration() const { return core::kMaxSessionDuration; }

int SessionEditorViewModel::durationStep() const { return kDurationStep; }

QVariantList SessionEditorViewModel::typeOptions() const {
    return CalendarTexts::sessionTypeOptions();
}

QString SessionEditorViewModel::dateError() const { return errorText(std::string(kDate)); }

QString SessionEditorViewModel::startTimeError() const {
    return errorText(std::string(kStartTime));
}

QString SessionEditorViewModel::durationError() const { return errorText(std::string(kDuration)); }

void SessionEditorViewModel::startAdding(const QString& date) {
    const core::TrainingSessionDraft defaults;
    Fields fields;
    fields.date = date;
    fields.durationMin = defaults.durationMin;
    fields.type = defaults.type;
    load(std::move(fields), 0);
}

bool SessionEditorViewModel::startEditing(qint64 sessionId) {
    const auto session = m_calendar.session(core::TrainingSessionId{sessionId});
    if (!session) {
        report(session.error());
        return false;
    }
    Fields fields;
    fields.date = IsoDates::toIso(session->date);
    fields.startTime = IsoDates::timeToIso(session->startTime);
    fields.durationMin = session->durationMin;
    fields.type = session->type;
    fields.note = QString::fromStdString(session->note.value_or(std::string{}));
    load(std::move(fields), sessionId);
    return true;
}

bool SessionEditorViewModel::save() {
    if (!m_session.isAllowed(core::Permission::EditCalendar)) {
        report(core::Error{core::ErrorCode::PermissionDenied,
                           "refused by the viewmodel: EditCalendar is needed",
                           {}});
        return false;
    }
    m_errors.clear();
    core::TrainingSessionDraft draft;
    // What does not parse stays an empty date, or a null duration reads as 0: validate() refuses
    // both.
    draft.date = IsoDates::parse(m_fields.date).value_or(core::Date{});
    if (!m_fields.startTime.isEmpty()) {
        draft.startTime = IsoDates::parseTime(m_fields.startTime);
        if (!draft.startTime) {
            addFieldError(std::string(kStartTime));
        }
    }
    draft.durationMin = m_fields.durationMin.toInt();
    draft.type = m_fields.type;
    draft.note = m_fields.note.toStdString();
    for (const core::ValidationError& error : core::validate(draft)) {
        m_errors.emplace(error.field, core::Error{error.code, "invalid value", error.field});
    }
    emit errorsChanged();
    if (!m_errors.empty()) {
        return false;
    }

    qint64 savedId = m_sessionId;
    core::Status status;
    if (adding()) {
        const auto id = m_calendar.addSession(core::TeamId{m_teamId}, draft);
        savedId = id ? id->value : 0;
        status = id ? core::Status{} : core::Status{std::unexpected(id.error())};
    } else {
        status = m_calendar.updateSession(core::TrainingSessionId{m_sessionId}, draft);
    }
    if (!status) {
        report(status.error());
        return false;
    }
    m_loaded = m_fields;
    emit dirtyChanged();
    emit saved(savedId, m_fields.date);
    return true;
}

bool SessionEditorViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        emit optionsChanged();
        emit errorsChanged();
    }
    return QObject::eventFilter(watched, event);
}

void SessionEditorViewModel::load(Fields fields, qint64 sessionId) {
    m_sessionId = sessionId;
    m_fields = std::move(fields);
    m_loaded = m_fields;
    m_errors.clear();
    emit fieldsChanged();
    emit durationMinChanged();
    emit dirtyChanged();
    emit errorsChanged();
}

void SessionEditorViewModel::clearError(const std::string& field) {
    if (m_errors.erase(field) > 0) {
        emit errorsChanged();
    }
}

QString SessionEditorViewModel::errorText(const std::string& field) const {
    const auto error = m_errors.find(field);
    return error != m_errors.end() ? ErrorPresenter::message(error->second) : QString{};
}

void SessionEditorViewModel::addFieldError(const std::string& field) {
    m_errors.insert_or_assign(field,
                              core::Error{core::ErrorCode::InvalidInput, "invalid value", field});
}

void SessionEditorViewModel::report(const core::Error& error) {
    if (isEditorField(error.field)) {
        m_errors.insert_or_assign(error.field, error);
        emit errorsChanged();
        return;
    }
    if (error.code != core::ErrorCode::Conflict) {
        qCritical("SessionEditorViewModel: %s", error.detail.c_str());
    }
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
