#include "viewmodels/PlayerEditorViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/QmlValues.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace fdss::viewmodels {

namespace {

constexpr std::string_view kFirstName = "first_name";
constexpr std::string_view kLastName = "last_name";
constexpr std::string_view kShirtNumber = "shirt_number";
constexpr std::string_view kBirthYear = "birth_year";
constexpr std::string_view kSecondaryPositions = "secondary_positions";

std::optional<int> toOptionalInt(const QVariant& value) {
    return value.isNull() ? std::nullopt : std::optional(value.toInt());
}

QVariant fromOptionalInt(const std::optional<int>& value) {
    return value ? QVariant(*value) : QVariant{};
}

bool isEditorField(std::string_view field) {
    return field == kFirstName || field == kLastName || field == kShirtNumber ||
           field == kBirthYear || field == kSecondaryPositions;
}

} // namespace

PlayerEditorViewModel::PlayerEditorViewModel(QObject* parent)
    : QObject(parent), m_squad(ServiceRegistry::instance().squad()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    refreshLimits();
}

qint64 PlayerEditorViewModel::teamId() const { return m_teamId; }

void PlayerEditorViewModel::setTeamId(qint64 teamId) {
    if (teamId != m_teamId) {
        m_teamId = teamId;
        emit teamIdChanged();
    }
}

bool PlayerEditorViewModel::adding() const { return m_playerId == 0; }

QString PlayerEditorViewModel::firstName() const { return m_firstName; }

void PlayerEditorViewModel::setFirstName(const QString& name) {
    if (name != m_firstName) {
        m_firstName = name;
        clearError(std::string(kFirstName));
        emit fieldsChanged();
    }
}

QString PlayerEditorViewModel::lastName() const { return m_lastName; }

void PlayerEditorViewModel::setLastName(const QString& name) {
    if (name != m_lastName) {
        m_lastName = name;
        clearError(std::string(kLastName));
        emit fieldsChanged();
    }
}

QVariant PlayerEditorViewModel::shirtNumber() const { return m_shirtNumber; }

void PlayerEditorViewModel::setShirtNumber(const QVariant& number) {
    const QVariant value = numberOrNull(number);
    if (value != m_shirtNumber) {
        m_shirtNumber = value;
        clearError(std::string(kShirtNumber));
        emit shirtNumberChanged();
    }
}

QVariant PlayerEditorViewModel::birthYear() const { return m_birthYear; }

void PlayerEditorViewModel::setBirthYear(const QVariant& year) {
    const QVariant value = numberOrNull(year);
    if (value != m_birthYear) {
        m_birthYear = value;
        clearError(std::string(kBirthYear));
        emit birthYearChanged();
    }
}

QString PlayerEditorViewModel::primaryPosition() const {
    return QString::fromUtf8(core::toDbText(m_primaryPosition));
}

void PlayerEditorViewModel::setPrimaryPosition(const QString& position) {
    const auto parsed = core::fromDbText<core::Position>(position.toStdString());
    if (!parsed) {
        report(parsed.error());
        return;
    }
    if (*parsed != m_primaryPosition) {
        m_primaryPosition = *parsed;
        std::erase(m_secondaryPositions, m_primaryPosition);
        clearError(std::string(kSecondaryPositions));
        emit fieldsChanged();
        emit secondaryOptionsChanged();
    }
}

QStringList PlayerEditorViewModel::secondaryPositions() const {
    QStringList positions;
    for (const core::Position position : m_secondaryPositions) {
        positions.append(QString::fromUtf8(core::toDbText(position)));
    }
    return positions;
}

void PlayerEditorViewModel::setSecondaryPositions(const QStringList& positions) {
    std::vector<core::Position> parsedPositions;
    for (const QString& position : positions) {
        const auto parsed = core::fromDbText<core::Position>(position.toStdString());
        if (!parsed) {
            report(parsed.error());
            return;
        }
        parsedPositions.push_back(*parsed);
    }
    if (parsedPositions != m_secondaryPositions) {
        m_secondaryPositions = std::move(parsedPositions);
        clearError(std::string(kSecondaryPositions));
        emit fieldsChanged();
    }
}

QString PlayerEditorViewModel::foot() const { return QString::fromUtf8(core::toDbText(m_foot)); }

void PlayerEditorViewModel::setFoot(const QString& foot) {
    const auto parsed = core::fromDbText<core::PreferredFoot>(foot.toStdString());
    if (!parsed) {
        report(parsed.error());
        return;
    }
    if (*parsed != m_foot) {
        m_foot = *parsed;
        emit fieldsChanged();
    }
}

QString PlayerEditorViewModel::notes() const { return m_notes; }

void PlayerEditorViewModel::setNotes(const QString& notes) {
    if (notes != m_notes) {
        m_notes = notes;
        emit fieldsChanged();
    }
}

QVariantList PlayerEditorViewModel::positionOptions() const {
    return SquadTexts::positionOptions();
}

QVariantList PlayerEditorViewModel::secondaryPositionOptions() const {
    QVariantList options = SquadTexts::positionOptions();
    const QString primary = primaryPosition();
    options.removeIf([&primary](const QVariant& option) {
        return option.toMap().value(QStringLiteral("value")).toString() == primary;
    });
    return options;
}

QVariantList PlayerEditorViewModel::footOptions() const { return SquadTexts::footOptions(); }

int PlayerEditorViewModel::minShirtNumber() const { return core::kMinShirtNumber; }

int PlayerEditorViewModel::maxShirtNumber() const { return core::kMaxShirtNumber; }

int PlayerEditorViewModel::minBirthYear() const { return core::kMinBirthYear; }

int PlayerEditorViewModel::maxBirthYear() const { return m_maxBirthYear; }

QString PlayerEditorViewModel::firstNameError() const { return errorText(std::string(kFirstName)); }

QString PlayerEditorViewModel::lastNameError() const { return errorText(std::string(kLastName)); }

QString PlayerEditorViewModel::shirtNumberError() const {
    return errorText(std::string(kShirtNumber));
}

QString PlayerEditorViewModel::birthYearError() const { return errorText(std::string(kBirthYear)); }

QString PlayerEditorViewModel::secondaryPositionsError() const {
    return errorText(std::string(kSecondaryPositions));
}

void PlayerEditorViewModel::startAdding() { load(core::PlayerDraft{}, 0); }

bool PlayerEditorViewModel::startEditing(qint64 playerId) {
    const auto player = m_squad.player(core::PlayerId{playerId});
    if (!player) {
        report(player.error());
        return false;
    }
    load(player->player, playerId);
    return true;
}

bool PlayerEditorViewModel::save() {
    if (!m_session.isAllowed(core::Permission::EditSquad)) {
        report(core::Error{core::ErrorCode::PermissionDenied,
                           "refused by the viewmodel: EditSquad is needed",
                           {}});
        return false;
    }
    m_errors.clear();
    const core::PlayerDraft current = draft();
    for (const core::ValidationError& error :
         core::validate(current, static_cast<int>(m_clock.today().year()))) {
        m_errors.emplace(error.field, core::Error{error.code, "invalid value", error.field});
    }
    emit errorsChanged();
    if (!m_errors.empty()) {
        return false;
    }

    qint64 savedId = m_playerId;
    core::Status status;
    if (adding()) {
        const auto id = m_squad.addPlayer(core::TeamId{m_teamId}, current);
        savedId = id ? id->value : 0;
        status = id ? core::Status{} : core::Status{std::unexpected(id.error())};
    } else {
        status = m_squad.updatePlayer(core::PlayerId{m_playerId}, current);
    }
    if (!status) {
        report(status.error());
        return false;
    }
    emit saved(savedId);
    return true;
}

bool PlayerEditorViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit optionsChanged();
        emit secondaryOptionsChanged();
        emit errorsChanged();
    }
    return QObject::eventFilter(watched, event);
}

void PlayerEditorViewModel::refreshLimits() {
    const int maxBirthYear = static_cast<int>(m_clock.today().year()) - core::kYoungestPlayerAge;
    if (maxBirthYear != m_maxBirthYear) {
        m_maxBirthYear = maxBirthYear;
        emit maxBirthYearChanged();
    }
}

void PlayerEditorViewModel::load(const core::PlayerDraft& draft, qint64 playerId) {
    // Before the values, so a field checks a new value against the new limit.
    refreshLimits();
    m_playerId = playerId;
    m_firstName = QString::fromStdString(draft.firstName);
    m_lastName = QString::fromStdString(draft.lastName);
    m_shirtNumber = fromOptionalInt(draft.shirtNumber);
    m_birthYear = fromOptionalInt(draft.birthYear);
    m_primaryPosition = draft.primaryPosition;
    m_secondaryPositions = draft.secondaryPositions;
    m_foot = draft.foot;
    m_notes = QString::fromStdString(draft.notes.value_or(std::string{}));
    m_errors.clear();
    emit fieldsChanged();
    emit shirtNumberChanged();
    emit birthYearChanged();
    emit secondaryOptionsChanged();
    emit errorsChanged();
}

core::PlayerDraft PlayerEditorViewModel::draft() const {
    core::PlayerDraft result;
    result.firstName = m_firstName.toStdString();
    result.lastName = m_lastName.toStdString();
    result.shirtNumber = toOptionalInt(m_shirtNumber);
    result.birthYear = toOptionalInt(m_birthYear);
    result.primaryPosition = m_primaryPosition;
    result.secondaryPositions = m_secondaryPositions;
    result.foot = m_foot;
    result.notes = m_notes.toStdString();
    return result;
}

void PlayerEditorViewModel::clearError(const std::string& field) {
    if (m_errors.erase(field) > 0) {
        emit errorsChanged();
    }
}

QString PlayerEditorViewModel::errorText(const std::string& field) const {
    const auto error = m_errors.find(field);
    return error != m_errors.end() ? ErrorPresenter::message(error->second) : QString{};
}

void PlayerEditorViewModel::report(const core::Error& error) {
    if (isEditorField(error.field)) {
        m_errors.insert_or_assign(error.field, error);
        emit errorsChanged();
        return;
    }
    qCritical("PlayerEditorViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
