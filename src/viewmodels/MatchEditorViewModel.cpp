#include "viewmodels/MatchEditorViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "services/CalendarService.hpp"
#include "services/Clock.hpp"
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

constexpr std::string_view kOpponent = "opponent_id";
constexpr std::string_view kNewOpponent = "opponent_name";
// Not a field of the service: its errors arrive as opponent_name and are kept here apart from
// those of a new opponent.
constexpr std::string_view kRenameOpponent = "opponent_rename";
constexpr std::string_view kDate = "date";
constexpr std::string_view kKickoffTime = "kickoff_time";

bool isEditorField(std::string_view field) {
    return field == kOpponent || field == kNewOpponent || field == kDate || field == kKickoffTime;
}

} // namespace

MatchEditorViewModel::MatchEditorViewModel(QObject* parent)
    : QObject(parent), m_calendar(ServiceRegistry::instance().calendar()),
      m_session(ServiceRegistry::instance().session()), m_opponents(new OpponentListModel(this)) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 MatchEditorViewModel::teamId() const { return m_teamId; }

void MatchEditorViewModel::setTeamId(qint64 teamId) {
    if (teamId != m_teamId) {
        m_teamId = teamId;
        emit teamIdChanged();
    }
}

bool MatchEditorViewModel::adding() const { return m_matchId == 0; }

bool MatchEditorViewModel::readOnly() const {
    return !m_session.isAllowed(core::Permission::EditCalendar) ||
           m_status != core::MatchStatus::Planned;
}

qint64 MatchEditorViewModel::matchId() const { return m_matchId; }

qint64 MatchEditorViewModel::opponentId() const { return m_opponentId; }

void MatchEditorViewModel::setOpponentId(qint64 opponentId) {
    if (opponentId != m_opponentId) {
        m_opponentId = opponentId;
        clearError(std::string(kOpponent));
        emit fieldsChanged();
    }
}

QString MatchEditorViewModel::opponentName() const {
    for (int row = 0; row < m_opponents->rowCount(); ++row) {
        const QModelIndex index = m_opponents->index(row);
        if (index.data(OpponentListModel::ValueRole).toLongLong() == m_opponentId) {
            return index.data(OpponentListModel::TextRole).toString();
        }
    }
    return {};
}

QString MatchEditorViewModel::date() const { return m_date; }

bool MatchEditorViewModel::beforeToday() const {
    const auto date = IsoDates::parse(m_date);
    return date && *date < ServiceRegistry::instance().clock().today();
}

void MatchEditorViewModel::setDate(const QString& date) {
    if (date != m_date) {
        m_date = date;
        clearError(std::string(kDate));
        emit fieldsChanged();
    }
}

QString MatchEditorViewModel::kickoffTime() const { return m_kickoffTime; }

void MatchEditorViewModel::setKickoffTime(const QString& time) {
    if (time != m_kickoffTime) {
        m_kickoffTime = time;
        clearError(std::string(kKickoffTime));
        emit fieldsChanged();
    }
}

QString MatchEditorViewModel::venue() const { return QString::fromUtf8(core::toDbText(m_venue)); }

void MatchEditorViewModel::setVenue(const QString& venue) {
    const auto parsed = core::fromDbText<core::Venue>(venue.toStdString());
    if (!parsed) {
        report(parsed.error());
        return;
    }
    if (*parsed != m_venue) {
        m_venue = *parsed;
        emit fieldsChanged();
    }
}

QString MatchEditorViewModel::competition() const { return m_competition; }

void MatchEditorViewModel::setCompetition(const QString& competition) {
    if (competition != m_competition) {
        m_competition = competition;
        emit fieldsChanged();
    }
}

QString MatchEditorViewModel::statusText() const {
    return CalendarTexts::matchStatusName(m_status);
}

OpponentListModel* MatchEditorViewModel::opponents() const { return m_opponents; }

QStringList MatchEditorViewModel::competitionSuggestions() const {
    return m_competitionSuggestions;
}

QVariantList MatchEditorViewModel::venueOptions() const { return CalendarTexts::venueOptions(); }

QString MatchEditorViewModel::opponentError() const { return errorText(std::string(kOpponent)); }

QString MatchEditorViewModel::newOpponentError() const {
    return errorText(std::string(kNewOpponent));
}

bool MatchEditorViewModel::canRenameOpponent() const {
    return m_opponentId > 0 && m_session.isAllowed(core::Permission::EditCalendar);
}

QString MatchEditorViewModel::renameOpponentError() const {
    return errorText(std::string(kRenameOpponent));
}

QString MatchEditorViewModel::dateError() const { return errorText(std::string(kDate)); }

QString MatchEditorViewModel::kickoffTimeError() const {
    return errorText(std::string(kKickoffTime));
}

void MatchEditorViewModel::startAdding(const QString& date) {
    refreshChoices();
    m_matchId = 0;
    m_opponentId = 0;
    m_date = date;
    m_kickoffTime.clear();
    m_venue = core::Venue::Home;
    m_competition.clear();
    m_status = core::MatchStatus::Planned;
    m_errors.clear();
    emit fieldsChanged();
    emit statusTextChanged();
    emit errorsChanged();
}

bool MatchEditorViewModel::startEditing(qint64 matchId) {
    const auto match = m_calendar.match(core::MatchId{matchId});
    if (!match) {
        report(match.error());
        return false;
    }
    refreshChoices();
    m_matchId = matchId;
    m_opponentId = match->opponentId.value;
    m_date = IsoDates::toIso(match->date);
    m_kickoffTime = IsoDates::timeToIso(match->kickoffTime);
    m_venue = match->venue;
    m_competition = QString::fromStdString(match->competition.value_or(std::string{}));
    m_status = match->status;
    m_errors.clear();
    emit fieldsChanged();
    emit statusTextChanged();
    emit errorsChanged();
    return true;
}

bool MatchEditorViewModel::addOpponent(const QString& name) {
    if (!m_session.isAllowed(core::Permission::EditCalendar)) {
        report(core::Error{core::ErrorCode::PermissionDenied,
                           "refused by the viewmodel: EditCalendar is needed",
                           {}});
        return false;
    }
    core::OpponentDraft draft;
    draft.name = name.toStdString();
    const auto id = m_calendar.addOpponent(draft);
    if (!id) {
        report(id.error());
        return false;
    }
    clearError(std::string(kNewOpponent));
    refreshChoices();
    m_opponentId = id->value;
    clearError(std::string(kOpponent));
    emit fieldsChanged();
    return true;
}

void MatchEditorViewModel::clearNewOpponentError() { clearError(std::string(kNewOpponent)); }

bool MatchEditorViewModel::renameOpponent(const QString& name) {
    if (!m_session.isAllowed(core::Permission::EditCalendar)) {
        report(core::Error{core::ErrorCode::PermissionDenied,
                           "refused by the viewmodel: EditCalendar is needed",
                           {}});
        return false;
    }
    const auto renamed =
        m_calendar.renameOpponent(core::OpponentId{m_opponentId}, name.toStdString());
    if (!renamed) {
        if (renamed.error().field == kNewOpponent) {
            m_errors.insert_or_assign(std::string(kRenameOpponent), renamed.error());
            emit errorsChanged();
        } else {
            report(renamed.error());
        }
        return false;
    }
    clearError(std::string(kRenameOpponent));
    refreshChoices();
    emit fieldsChanged();
    emit opponentRenamed();
    return true;
}

void MatchEditorViewModel::clearRenameOpponentError() { clearError(std::string(kRenameOpponent)); }

bool MatchEditorViewModel::save() {
    if (!m_session.isAllowed(core::Permission::EditCalendar)) {
        report(core::Error{core::ErrorCode::PermissionDenied,
                           "refused by the viewmodel: EditCalendar is needed",
                           {}});
        return false;
    }
    m_errors.clear();
    core::MatchDraft draft;
    draft.opponentId = core::OpponentId{m_opponentId};
    if (m_opponentId == 0) {
        addFieldError(std::string(kOpponent));
    }
    // What does not parse stays an empty date, which validate() refuses.
    draft.date = IsoDates::parse(m_date).value_or(core::Date{});
    if (!m_kickoffTime.isEmpty()) {
        draft.kickoffTime = IsoDates::parseTime(m_kickoffTime);
        if (!draft.kickoffTime) {
            addFieldError(std::string(kKickoffTime));
        }
    }
    draft.venue = m_venue;
    draft.competition = m_competition.toStdString();
    for (const core::ValidationError& error : core::validate(draft)) {
        m_errors.emplace(error.field, core::Error{error.code, "invalid value", error.field});
    }
    emit errorsChanged();
    if (!m_errors.empty()) {
        return false;
    }

    qint64 savedId = m_matchId;
    core::Status status;
    if (adding()) {
        const auto id = m_calendar.addMatch(core::TeamId{m_teamId}, draft);
        savedId = id ? id->value : 0;
        status = id ? core::Status{} : core::Status{std::unexpected(id.error())};
    } else {
        status = m_calendar.updateMatch(core::MatchId{m_matchId}, draft);
    }
    if (!status) {
        report(status.error());
        return false;
    }
    emit saved(savedId, m_date);
    return true;
}

bool MatchEditorViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit optionsChanged();
        emit statusTextChanged();
        emit errorsChanged();
    }
    return QObject::eventFilter(watched, event);
}

void MatchEditorViewModel::refreshChoices() {
    if (auto opponents = m_calendar.listOpponents()) {
        m_opponents->setOpponents(std::move(*opponents));
    } else {
        report(opponents.error());
    }
    m_competitionSuggestions.clear();
    if (const auto competitions = m_calendar.listCompetitions(core::TeamId{m_teamId})) {
        for (const std::string& competition : *competitions) {
            m_competitionSuggestions.append(QString::fromStdString(competition));
        }
    } else {
        report(competitions.error());
    }
    emit suggestionsChanged();
}

void MatchEditorViewModel::clearError(const std::string& field) {
    if (m_errors.erase(field) > 0) {
        emit errorsChanged();
    }
}

QString MatchEditorViewModel::errorText(const std::string& field) const {
    const auto error = m_errors.find(field);
    return error != m_errors.end() ? ErrorPresenter::message(error->second) : QString{};
}

void MatchEditorViewModel::addFieldError(const std::string& field) {
    m_errors.insert_or_assign(field,
                              core::Error{core::ErrorCode::InvalidInput, "invalid value", field});
}

void MatchEditorViewModel::report(const core::Error& error) {
    if (isEditorField(error.field)) {
        m_errors.insert_or_assign(error.field, error);
        emit errorsChanged();
        return;
    }
    if (error.code != core::ErrorCode::Conflict) {
        qCritical("MatchEditorViewModel: %s", error.detail.c_str());
    }
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
