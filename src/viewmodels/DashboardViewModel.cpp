#include "viewmodels/DashboardViewModel.hpp"

#include "core/algorithms/LineupValidator.hpp"
#include "core/domain/Permissions.hpp"
#include "services/Session.hpp"
#include "viewmodels/CalendarTexts.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/LoadTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <array>
#include <map>
#include <ranges>

namespace fdss::viewmodels {

namespace {

// The rating of the match on its own row and in the summary.
constexpr int kRatingDecimals = 1;

QString keyOf(const char* text) { return QString::fromLatin1(text); }

} // namespace

DashboardViewModel::DashboardViewModel(QObject* parent)
    : QObject(parent), m_dashboard(ServiceRegistry::instance().dashboard()),
      m_load(ServiceRegistry::instance().load()), m_lineups(ServiceRegistry::instance().lineup()),
      m_session(ServiceRegistry::instance().session()),
      m_yellowCardThreshold(core::kDefaultYellowCardThreshold) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 DashboardViewModel::teamId() const { return m_teamId; }

void DashboardViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    reload();
}

int DashboardViewModel::yellowCardThreshold() const { return m_yellowCardThreshold; }

void DashboardViewModel::setYellowCardThreshold(int threshold) {
    if (threshold == m_yellowCardThreshold) {
        return;
    }
    m_yellowCardThreshold = threshold;
    emit yellowCardThresholdChanged();
    reload();
}

bool DashboardViewModel::busy() const { return m_busy; }

bool DashboardViewModel::ready() const { return m_overview.has_value(); }

QString DashboardViewModel::readError() const {
    return m_readFailure ? ErrorPresenter::message(*m_readFailure) : QString{};
}

void DashboardViewModel::reload() {
    const int request = ++m_request;
    m_readFailure.reset();
    // ViewAnalysis: every role, and nobody logged out. The services check it again.
    if (m_teamId == 0 || !m_session.isAllowed(core::Permission::ViewAnalysis)) {
        clear();
        return;
    }
    auto overview = m_dashboard.overview(core::TeamId{m_teamId}, m_yellowCardThreshold);
    if (!overview) {
        qCritical("DashboardViewModel: %s", overview.error().detail.c_str());
        m_readFailure = overview.error();
        clear();
        emit errorOccurred(ErrorPresenter::message(overview.error()));
        return;
    }
    m_overview = std::move(*overview);
    emit contentChanged();

    auto future = m_load.teamLoad(core::TeamId{m_teamId});
    if (!future) {
        qCritical("DashboardViewModel: %s", future.error().detail.c_str());
        m_readFailure = future.error();
        clear();
        emit errorOccurred(ErrorPresenter::message(future.error()));
        return;
    }
    setBusy(true);
    future->then(this, [this, request](services::TeamLoad result) {
        if (request != m_request) {
            return;
        }
        m_teamLoad = std::move(result);
        setBusy(false);
        emit contentChanged();
    });
}

void DashboardViewModel::clear() {
    m_overview.reset();
    m_teamLoad.reset();
    setBusy(false);
    emit contentChanged();
}

void DashboardViewModel::setBusy(bool busy) {
    if (busy == m_busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

const services::NextMatchInfo* DashboardViewModel::next() const {
    return m_overview && m_overview->nextMatch ? &*m_overview->nextMatch : nullptr;
}

bool DashboardViewModel::hasNextMatch() const { return next() != nullptr; }

qint64 DashboardViewModel::nextMatchId() const {
    return next() != nullptr ? next()->match.match.id.value : 0;
}

bool DashboardViewModel::nextMatchLive() const {
    return next() != nullptr && next()->match.match.status == core::MatchStatus::Live;
}

QString DashboardViewModel::nextMatchDate() const {
    return next() != nullptr ? IsoDates::toIso(next()->match.match.date) : QString{};
}

QString DashboardViewModel::nextMatchDateText() const {
    if (next() == nullptr) {
        return {};
    }
    const core::Date date = next()->match.match.date;
    return tr("%1, %2").arg(CalendarTexts::dayName(date), IsoDates::displayText(date));
}

QString DashboardViewModel::nextMatchOpponent() const {
    return next() != nullptr ? QString::fromStdString(next()->match.opponentName) : QString{};
}

QString DashboardViewModel::nextMatchDetails() const {
    if (next() == nullptr) {
        return {};
    }
    const core::Match& match = next()->match.match;
    QStringList parts{CalendarTexts::venueName(match.venue)};
    if (match.kickoffTime) {
        parts.append(IsoDates::timeToIso(match.kickoffTime));
    }
    if (match.competition && !match.competition->empty()) {
        parts.append(QString::fromStdString(*match.competition));
    }
    //: Separates the venue, the kick-off and the competition of the next match.
    return parts.join(tr(" · "));
}

int DashboardViewModel::daysToNextMatch() const { return next() != nullptr ? next()->daysToGo : 0; }

QString DashboardViewModel::daysText() const {
    if (next() == nullptr) {
        return {};
    }
    if (nextMatchLive()) {
        return tr("Match in progress");
    }
    switch (next()->daysToGo) {
    case 0:
        return tr("Today");
    case 1:
        return tr("Tomorrow");
    default:
        //: Days to the next match, 2 or more: "Za %1 dni" fits every such number in Polish.
        return tr("In %1 days").arg(next()->daysToGo);
    }
}

QString DashboardViewModel::nextMatchSection() const {
    return nextMatchLive() ? QStringLiteral("matchday") : QStringLiteral("prematch");
}

bool DashboardViewModel::lineupSaved() const {
    return next() != nullptr && next()->lineup && next()->lineup->saved;
}

int DashboardViewModel::countIssues(bool errorsOnly) const {
    if (next() == nullptr || !next()->lineup) {
        return 0;
    }
    const services::MatchLineup& lineup = *next()->lineup;
    core::LineupContext context = lineup.context;
    if (m_teamLoad) {
        std::map<core::PlayerId, core::LoadStatus> statuses;
        for (const services::PlayerLoadRow& row : m_teamLoad->players) {
            statuses.emplace(row.player.id, row.assessment.verdict.status);
        }
        for (auto& [player, facts] : context.players) {
            const auto found = statuses.find(player);
            facts.loadStatus = found == statuses.end()
                                   ? std::nullopt
                                   : std::optional<core::LoadStatus>(found->second);
        }
    }
    const std::vector<core::LineupIssue> issues = m_lineups.check(lineup.lineup, context);
    if (!errorsOnly) {
        return static_cast<int>(issues.size());
    }
    return static_cast<int>(
        std::ranges::count(issues, core::LineupSeverity::Error, &core::LineupIssue::severity));
}

bool DashboardViewModel::lineupReady() const { return lineupSaved() && countIssues(true) == 0; }

int DashboardViewModel::lineupIssueCount() const { return countIssues(false); }

QString DashboardViewModel::lineupText() const {
    if (next() == nullptr || nextMatchLive()) {
        return {};
    }
    if (!lineupSaved()) {
        return tr("No lineup saved yet");
    }
    const int issues = lineupIssueCount();
    if (!lineupReady()) {
        return tr("Lineup issues: %1").arg(issues);
    }
    return issues == 0 ? tr("Lineup ready") : tr("Lineup ready, warnings: %1").arg(issues);
}

QString DashboardViewModel::lineupStatusKey() const {
    if (next() == nullptr || nextMatchLive()) {
        return {};
    }
    if (!lineupReady()) {
        return lineupSaved() ? QStringLiteral("danger") : QStringLiteral("warn");
    }
    return lineupIssueCount() == 0 ? QStringLiteral("ok") : QStringLiteral("warn");
}

QVariantMap DashboardViewModel::emptyState(const QString& text, const QString& actionText,
                                           const QString& section) const {
    return {{QStringLiteral("text"), text},
            {QStringLiteral("actionText"), actionText},
            {QStringLiteral("actionSection"), section}};
}

QVariantMap DashboardViewModel::nextMatchEmpty() const {
    if (m_session.isAllowed(core::Permission::EditCalendar)) {
        return emptyState(tr("No match is planned. Add the next match in the calendar."),
                          tr("Open the calendar"), QStringLiteral("calendar"));
    }
    return emptyState(
        tr("No match is planned. Ask the coach or the assistant to add the next match."), {},
        QStringLiteral("calendar"));
}

QVariantList DashboardViewModel::unavailable() const {
    QVariantList rows;
    if (!m_overview) {
        return rows;
    }
    for (const services::UnavailablePlayer& row : m_overview->unavailable) {
        const core::PlayerStatus status =
            core::playerStatusOn(std::span(&row.entry, 1), m_overview->today);
        rows.append(QVariantMap{
            {QStringLiteral("playerId"), static_cast<qint64>(row.player.id.value)},
            {QStringLiteral("name"), SquadTexts::displayName(row.player)},
            {QStringLiteral("statusKey"), SquadTexts::statusKey(status, false)},
            {QStringLiteral("typeText"), SquadTexts::availabilityTypeName(row.entry.type)},
            {QStringLiteral("returnText"),
             row.entry.expectedTo
                 ? tr("Expected back: %1").arg(IsoDates::displayText(row.entry.expectedTo))
                 : tr("Return date not known")},
            {QStringLiteral("description"),
             row.entry.description ? QString::fromStdString(*row.entry.description) : QString{}},
        });
    }
    return rows;
}

QVariantMap DashboardViewModel::unavailableEmpty() const {
    if (m_session.isAllowed(core::Permission::EditAvailability)) {
        return emptyState(tr("Everyone is available today. Injuries, illnesses and absences are "
                             "entered on the player card."),
                          tr("Open the squad"), QStringLiteral("squad"));
    }
    return emptyState(tr("Everyone is available today. The coach, the assistant or the physio "
                         "enters injuries, illnesses and absences."),
                      {}, QStringLiteral("squad"));
}

QVariantList DashboardViewModel::loadSignals() const {
    QVariantList rows;
    if (!m_teamLoad) {
        return rows;
    }
    std::vector<const services::PlayerLoadRow*> signalRows;
    for (const services::PlayerLoadRow& row : m_teamLoad->players) {
        const core::LoadStatus status = row.assessment.verdict.status;
        if (status == core::LoadStatus::High || status == core::LoadStatus::Elevated) {
            signalRows.push_back(&row);
        }
    }
    // Red first; the order of the squad within a status.
    std::ranges::stable_sort(signalRows, {}, [](const services::PlayerLoadRow* row) {
        return LoadTexts::statusRank(row->assessment.verdict.status);
    });
    for (const services::PlayerLoadRow* row : signalRows) {
        rows.append(QVariantMap{
            {QStringLiteral("playerId"), static_cast<qint64>(row->player.id.value)},
            {QStringLiteral("name"), SquadTexts::displayName(row->player)},
            {QStringLiteral("statusKey"), LoadTexts::statusKey(row->assessment.verdict.status)},
            {QStringLiteral("statusText"), LoadTexts::statusName(row->assessment.verdict)},
            {QStringLiteral("explanation"),
             LoadTexts::explanation(row->assessment, m_teamLoad->thresholds)},
        });
    }
    return rows;
}

QVariantMap DashboardViewModel::loadSignalsEmpty() const {
    // ViewAnalysis: every role opens the Load screen.
    if (m_teamLoad && std::ranges::all_of(m_teamLoad->players, [](const auto& row) {
            return row.assessment.verdict.status == core::LoadStatus::NotEnoughData;
        })) {
        // As on the P11 team screen, the player closest to a status supplies the hint.
        const auto closest = std::ranges::min_element(m_teamLoad->players, {}, [](const auto& row) {
            return std::pair(row.assessment.rpeDaysMissing,
                             row.assessment.baselineFrom.value_or(core::kMaxSupportedDate));
        });
        const QString hint = closest != m_teamLoad->players.end()
                                 ? LoadTexts::hint(closest->assessment)
                                 : tr("Enter RPE after sessions to build the load comparison.");
        return emptyState(tr("Not enough data. %1").arg(hint), tr("Open load"),
                          QStringLiteral("load"));
    }
    return emptyState(tr("No load signals today. The Load screen shows the whole team."),
                      tr("Open load"), QStringLiteral("load"));
}

QVariantList DashboardViewModel::form() const {
    QVariantList rows;
    if (!m_overview) {
        return rows;
    }
    for (const services::FormMatch& item : m_overview->lastMatches) {
        const core::Match& match = item.match.match;
        QString key = QStringLiteral("none");
        //: No result stored: shown instead of W, D or L.
        QString letter = tr("–");
        QString resultText = tr("No result");
        QString scoreText = tr("–");
        if (match.goalsFor && match.goalsAgainst) {
            scoreText = tr("%1:%2").arg(*match.goalsFor).arg(*match.goalsAgainst);
            if (*match.goalsFor > *match.goalsAgainst) {
                key = keyOf("win");
                //: Win, one letter (Polish: Z).
                letter = tr("W");
                resultText = tr("Win");
            } else if (*match.goalsFor == *match.goalsAgainst) {
                key = keyOf("draw");
                //: Draw, one letter (Polish: R).
                letter = tr("D");
                resultText = tr("Draw");
            } else {
                key = keyOf("loss");
                //: Loss, one letter (Polish: P).
                letter = tr("L");
                resultText = tr("Loss");
            }
        }
        rows.append(QVariantMap{
            {QStringLiteral("matchId"), static_cast<qint64>(match.id.value)},
            {QStringLiteral("resultKey"), key},
            {QStringLiteral("letter"), letter},
            {QStringLiteral("resultText"), resultText},
            {QStringLiteral("scoreText"), scoreText},
            {QStringLiteral("opponent"), QString::fromStdString(item.match.opponentName)},
            {QStringLiteral("dateText"), IsoDates::displayText(match.date)},
            {QStringLiteral("ratingText"),
             item.averageRating
                 ? tr("Rating: %1").arg(LoadTexts::decimal(*item.averageRating, kRatingDecimals))
                 : tr("No ratings")},
        });
    }
    return rows;
}

QString DashboardViewModel::formSummary() const {
    if (!m_overview || m_overview->lastMatches.empty()) {
        return {};
    }
    int goalsFor = 0;
    int goalsAgainst = 0;
    double ratingSum = 0.0;
    int rated = 0;
    int withResult = 0;
    for (const services::FormMatch& item : m_overview->lastMatches) {
        const core::Match& match = item.match.match;
        if (match.goalsFor && match.goalsAgainst) {
            goalsFor += *match.goalsFor;
            goalsAgainst += *match.goalsAgainst;
            ++withResult;
        }
        if (item.averageRating) {
            ratingSum += *item.averageRating;
            ++rated;
        }
    }
    const QString goals = withResult == 0 ? tr("No results recorded")
                          : withResult == static_cast<int>(m_overview->lastMatches.size())
                              ? tr("Goals: %1:%2").arg(goalsFor).arg(goalsAgainst)
                              : tr("Goals: %1:%2 (matches with a result: %3 of %4)")
                                    .arg(goalsFor)
                                    .arg(goalsAgainst)
                                    .arg(withResult)
                                    .arg(m_overview->lastMatches.size());
    if (rated == 0) {
        return goals;
    }
    return tr("%1 · Average rating: %2")
        .arg(goals, LoadTexts::decimal(ratingSum / rated, kRatingDecimals));
}

QVariantList DashboardViewModel::todaySessions() const {
    QVariantList rows;
    if (!m_overview) {
        return rows;
    }
    for (const core::TrainingSession& session : m_overview->todaySessions) {
        rows.append(QVariantMap{
            {QStringLiteral("sessionId"), static_cast<qint64>(session.id.value)},
            {QStringLiteral("timeText"),
             session.startTime ? IsoDates::timeToIso(session.startTime) : tr("No start time")},
            {QStringLiteral("typeText"), CalendarTexts::sessionTypeName(session.type)},
            {QStringLiteral("durationText"), tr("%1 min").arg(session.durationMin)},
        });
    }
    return rows;
}

QVariantMap DashboardViewModel::todayEmpty() const {
    if (m_session.isAllowed(core::Permission::EditCalendar)) {
        return emptyState(tr("No session today. Plan sessions in the calendar."),
                          tr("Open the calendar"), QStringLiteral("calendar"));
    }
    return emptyState(tr("No session today. Ask the coach or the assistant to plan a session."), {},
                      QStringLiteral("calendar"));
}

bool DashboardViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit contentChanged();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
