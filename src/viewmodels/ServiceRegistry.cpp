#include "viewmodels/ServiceRegistry.hpp"

#include "data/db/DatabaseManager.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/CapabilityRepository.hpp"
#include "data/repositories/ClubRepository.hpp"
#include "data/repositories/ImportLogRepository.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchLiveRepository.hpp"
#include "data/repositories/MatchNoteRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MatchTacticRepository.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/CalendarService.hpp"
#include "services/CapabilityService.hpp"
#include "services/DashboardService.hpp"
#include "services/DemoDataService.hpp"
#include "services/LineupService.hpp"
#include "services/LoadService.hpp"
#include "services/MatchDayService.hpp"
#include "services/MatchReportService.hpp"
#include "services/MetricService.hpp"
#include "services/Session.hpp"
#include "services/SetupService.hpp"
#include "services/SquadService.hpp"
#include "services/TrainingService.hpp"
#include "services/import/ImportService.hpp"

#include <QtGlobal>

#include <utility>

namespace fdss::viewmodels {

namespace {

std::unique_ptr<ServiceRegistry> g_instance;

std::unique_ptr<services::Clock> clockOrSystem(std::unique_ptr<services::Clock> clock) {
    if (clock) {
        return clock;
    }
    return std::make_unique<services::SystemClock>();
}

} // namespace

std::unique_ptr<ServiceRegistry>
ServiceRegistry::create(std::unique_ptr<data::DatabaseManager> database, RegistryOptions options) {
    return std::make_unique<ServiceRegistry>(PassKey{}, std::move(database), std::move(options));
}

void ServiceRegistry::install(std::unique_ptr<ServiceRegistry> registry) {
    if (g_instance) {
        qFatal("ServiceRegistry: already installed; call reset() first");
    }
    g_instance = std::move(registry);
}

ServiceRegistry& ServiceRegistry::instance() {
    if (!g_instance) {
        qFatal("ServiceRegistry: nothing installed; install it before QML loads");
    }
    return *g_instance;
}

void ServiceRegistry::reset() { g_instance.reset(); }

bool ServiceRegistry::isInstalled() { return g_instance != nullptr; }

ServiceRegistry::ServiceRegistry(PassKey, std::unique_ptr<data::DatabaseManager> database,
                                 RegistryOptions options)
    : m_database(std::move(database)), m_clock(clockOrSystem(std::move(options.clock))),
      m_clubs(std::make_unique<data::ClubRepository>(*m_database)),
      m_staff(std::make_unique<data::StaffRepository>(*m_database)),
      m_players(std::make_unique<data::PlayerRepository>(*m_database)),
      m_availability(std::make_unique<data::AvailabilityRepository>(*m_database)),
      m_opponents(std::make_unique<data::OpponentRepository>(*m_database)),
      m_matches(std::make_unique<data::MatchRepository>(*m_database)),
      m_training(std::make_unique<data::TrainingRepository>(*m_database)),
      m_lineups(std::make_unique<data::LineupRepository>(*m_database)),
      m_matchLive(std::make_unique<data::MatchLiveRepository>(*m_database)),
      m_matchNotes(std::make_unique<data::MatchNoteRepository>(*m_database)),
      m_matchTactics(std::make_unique<data::MatchTacticRepository>(*m_database)),
      m_capabilityCounts(std::make_unique<data::CapabilityRepository>(*m_database)),
      m_importLog(std::make_unique<data::ImportLogRepository>(*m_database)),
      m_metricStore(std::make_unique<data::MetricRepository>(*m_database)),
      m_session(std::make_unique<services::Session>()),
      m_auth(std::make_unique<services::AuthService>(*m_database, *m_staff, *m_session, *m_clock,
                                                     options.pinIterations)),
      m_setup(std::make_unique<services::SetupService>(*m_database, *m_clubs, *m_staff, *m_auth,
                                                       *m_session)),
      m_squad(std::make_unique<services::SquadService>(*m_players, *m_availability, *m_session,
                                                       *m_clock)),
      m_calendar(std::make_unique<services::CalendarService>(*m_database, *m_opponents, *m_matches,
                                                             *m_training, *m_lineups, *m_session)),
      m_trainingService(std::make_unique<services::TrainingService>(
          *m_database, *m_training, *m_players, *m_availability, *m_session)),
      m_demoData(std::make_unique<services::DemoDataService>(
          *m_database, *m_players, *m_availability, *m_opponents, *m_matches, *m_training, *m_staff,
          *m_auth, *m_session, *m_clock)),
      m_load(std::make_unique<services::LoadService>(*m_players, *m_training, *m_matches,
                                                     *m_session, *m_clock)),
      m_lineup(std::make_unique<services::LineupService>(*m_database, *m_lineups, *m_matches,
                                                         *m_opponents, *m_players, *m_availability,
                                                         *m_training, *m_session, *m_clock)),
      m_matchDay(std::make_unique<services::MatchDayService>(
          *m_database, *m_matches, *m_matchLive, *m_matchNotes, *m_matchTactics, *m_lineups,
          *m_clubs, *m_opponents, *m_players, *m_lineup, *m_session, *m_clock)),
      m_matchReport(std::make_unique<services::MatchReportService>(
          *m_database, *m_matches, *m_matchLive, *m_matchNotes, *m_matchTactics, *m_lineups,
          *m_opponents, *m_players, *m_session)),
      m_capabilities(
          std::make_unique<services::CapabilityService>(*m_capabilityCounts, *m_session, *m_clock)),
      m_dashboard(std::make_unique<services::DashboardService>(*m_matches, *m_opponents, *m_players,
                                                               *m_availability, *m_training,
                                                               *m_lineup, *m_session, *m_clock)),
      m_imports(std::make_unique<services::ImportService>(
          *m_database,
          services::ImportRepositories{*m_importLog, *m_players, *m_training, *m_matches,
                                       *m_matchLive, *m_metricStore, *m_opponents},
          *m_session, *m_clock)),
      m_metrics(std::make_unique<services::MetricService>(
          *m_database, *m_metricStore, *m_players, *m_training, *m_matches, *m_session, *m_clock)) {
}

ServiceRegistry::~ServiceRegistry() = default;

const services::Clock& ServiceRegistry::clock() const { return *m_clock; }

services::Session& ServiceRegistry::session() { return *m_session; }

services::AuthService& ServiceRegistry::auth() { return *m_auth; }

services::SetupService& ServiceRegistry::setup() { return *m_setup; }

services::SquadService& ServiceRegistry::squad() { return *m_squad; }

services::CalendarService& ServiceRegistry::calendar() { return *m_calendar; }

services::TrainingService& ServiceRegistry::training() { return *m_trainingService; }

services::DemoDataService& ServiceRegistry::demoData() { return *m_demoData; }

services::LoadService& ServiceRegistry::load() { return *m_load; }
services::LineupService& ServiceRegistry::lineup() { return *m_lineup; }

services::MatchDayService& ServiceRegistry::matchDay() { return *m_matchDay; }

services::MatchReportService& ServiceRegistry::matchReport() { return *m_matchReport; }

services::CapabilityService& ServiceRegistry::capabilities() { return *m_capabilities; }

services::DashboardService& ServiceRegistry::dashboard() { return *m_dashboard; }
services::ImportService& ServiceRegistry::imports() { return *m_imports; }
services::MetricService& ServiceRegistry::metrics() { return *m_metrics; }

} // namespace fdss::viewmodels
