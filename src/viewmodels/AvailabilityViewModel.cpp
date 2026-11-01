#include "viewmodels/AvailabilityViewModel.hpp"

#include "core/domain/Enums.hpp"
#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <string_view>
#include <utility>

namespace fdss::viewmodels {

namespace {

constexpr std::string_view kFromDate = "from_date";
constexpr std::string_view kExpectedTo = "expected_to";
constexpr std::string_view kActualTo = "actual_to";

bool isFormField(std::string_view field) {
    return field == kFromDate || field == kExpectedTo || field == kActualTo;
}

core::Error invalidField(std::string_view field) {
    return core::Error{core::ErrorCode::InvalidInput, "invalid value", std::string(field)};
}

} // namespace

AvailabilityViewModel::AvailabilityViewModel(QObject* parent)
    : QObject(parent), m_squad(ServiceRegistry::instance().squad()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()), m_entries(new AvailabilityListModel(this)) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 AvailabilityViewModel::playerId() const { return m_playerId; }

void AvailabilityViewModel::setPlayerId(qint64 playerId) {
    if (playerId == m_playerId) {
        return;
    }
    m_playerId = playerId;
    emit playerIdChanged();
    reload();
}

AvailabilityListModel* AvailabilityViewModel::entries() const { return m_entries; }

QVariantList AvailabilityViewModel::typeOptions() const {
    return SquadTexts::availabilityTypeOptions();
}

bool AvailabilityViewModel::canEdit() const {
    return m_session.isAllowed(core::Permission::EditAvailability);
}

bool AvailabilityViewModel::canViewDetails() const {
    return m_session.isAllowed(core::Permission::ViewAvailabilityDetails);
}

QString AvailabilityViewModel::today() const { return IsoDates::toIso(m_clock.today()); }

QString AvailabilityViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

QString AvailabilityViewModel::fromDateError() const { return errorText(std::string(kFromDate)); }

QString AvailabilityViewModel::expectedToError() const {
    return errorText(std::string(kExpectedTo));
}

QString AvailabilityViewModel::actualToError() const { return errorText(std::string(kActualTo)); }

void AvailabilityViewModel::reload() {
    m_loadFailure.reset();
    if (m_playerId == 0 || !m_session.user()) {
        m_entries->setEntries({});
    } else if (auto entries = m_squad.listAvailability(core::PlayerId{m_playerId})) {
        m_entries->setEntries(std::move(*entries));
    } else {
        qCritical("AvailabilityViewModel: %s", entries.error().detail.c_str());
        m_entries->setEntries({});
        m_loadFailure = entries.error();
    }
    emit entriesChanged();
    emit loadErrorChanged();
}

bool AvailabilityViewModel::add(const QString& type, const QString& fromDate,
                                const QString& expectedTo, const QString& description) {
    clearErrors();
    core::AvailabilityDraft draft;
    if (!checkAllowed() || !fillDraft(type, fromDate, expectedTo, {}, description, draft)) {
        return false;
    }
    const auto id = m_squad.addAvailability(core::PlayerId{m_playerId}, draft);
    return finish(id ? core::Status{} : core::Status{std::unexpected(id.error())});
}

bool AvailabilityViewModel::update(qint64 entryId, const QString& type, const QString& fromDate,
                                   const QString& expectedTo, const QString& actualTo,
                                   const QString& description) {
    clearErrors();
    core::AvailabilityDraft draft;
    if (!checkAllowed() || !fillDraft(type, fromDate, expectedTo, actualTo, description, draft)) {
        return false;
    }
    return finish(m_squad.updateAvailability(core::AvailabilityId{entryId}, draft));
}

bool AvailabilityViewModel::markReturned(qint64 entryId, const QString& actualTo) {
    clearErrors();
    if (!checkAllowed()) {
        return false;
    }
    const std::optional<core::Date> date = IsoDates::parse(actualTo);
    if (!date) {
        report(invalidField(kActualTo));
        return false;
    }
    return finish(
        m_squad.markReturned(core::PlayerId{m_playerId}, core::AvailabilityId{entryId}, *date));
}

QString AvailabilityViewModel::defaultReturnDate(qint64 entryId) const {
    const core::Date today = m_clock.today();
    const core::Availability* entry = m_entries->find(entryId);
    return IsoDates::toIso(entry != nullptr ? std::max(today, entry->fromDate) : today);
}

void AvailabilityViewModel::clearErrors() {
    m_errors.clear();
    emit errorsChanged();
}

bool AvailabilityViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        emit optionsChanged();
        emit loadErrorChanged();
        emit errorsChanged();
    }
    return QObject::eventFilter(watched, event);
}

bool AvailabilityViewModel::checkAllowed() {
    if (canEdit()) {
        return true;
    }
    report(core::Error{core::ErrorCode::PermissionDenied,
                       "refused by the viewmodel: EditAvailability is needed",
                       {}});
    return false;
}

bool AvailabilityViewModel::readDate(const QString& iso, const std::string& field,
                                     std::optional<core::Date>& date) {
    date.reset();
    if (iso.isEmpty()) {
        return true;
    }
    date = IsoDates::parse(iso);
    if (date) {
        return true;
    }
    report(invalidField(field));
    return false;
}

bool AvailabilityViewModel::fillDraft(const QString& type, const QString& fromDate,
                                      const QString& expectedTo, const QString& actualTo,
                                      const QString& description, core::AvailabilityDraft& draft) {
    const auto parsedType = core::fromDbText<core::AvailabilityType>(type.toStdString());
    if (!parsedType) {
        report(parsedType.error());
        return false;
    }
    draft.type = *parsedType;
    const std::optional<core::Date> from = IsoDates::parse(fromDate);
    if (!from) {
        report(invalidField(kFromDate));
        return false;
    }
    draft.fromDate = *from;
    if (!readDate(expectedTo, std::string(kExpectedTo), draft.expectedTo) ||
        !readDate(actualTo, std::string(kActualTo), draft.actualTo)) {
        return false;
    }
    if (canViewDetails()) {
        draft.description = description.toStdString();
    }
    bool valid = true;
    for (const core::ValidationError& error : core::validate(draft)) {
        report(invalidField(error.field));
        valid = false;
    }
    return valid;
}

bool AvailabilityViewModel::finish(const core::Status& status) {
    if (!status) {
        report(status.error());
        return false;
    }
    reload();
    emit availabilityChanged();
    return true;
}

QString AvailabilityViewModel::errorText(const std::string& field) const {
    const auto error = m_errors.find(field);
    return error != m_errors.end() ? ErrorPresenter::message(error->second) : QString{};
}

void AvailabilityViewModel::report(const core::Error& error) {
    if (isFormField(error.field)) {
        m_errors.insert_or_assign(error.field, error);
        emit errorsChanged();
        return;
    }
    qCritical("AvailabilityViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
