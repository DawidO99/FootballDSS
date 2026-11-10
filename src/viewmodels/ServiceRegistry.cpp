#include "viewmodels/ServiceRegistry.hpp"

#include "data/db/DatabaseManager.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/ClubRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/CalendarService.hpp"
#include "services/DemoDataService.hpp"
#include "services/Session.hpp"
#include "services/SetupService.hpp"
#include "services/SquadService.hpp"
#include "services/TrainingService.hpp"

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
      m_session(std::make_unique<services::Session>()),
      m_auth(std::make_unique<services::AuthService>(*m_database, *m_staff, *m_session, *m_clock,
                                                     options.pinIterations)),
      m_setup(std::make_unique<services::SetupService>(*m_database, *m_clubs, *m_staff, *m_auth,
                                                       *m_session)),
      m_squad(std::make_unique<services::SquadService>(*m_players, *m_availability, *m_session,
                                                       *m_clock)),
      m_calendar(std::make_unique<services::CalendarService>(*m_database, *m_opponents, *m_matches,
                                                             *m_training, *m_session)),
      m_trainingService(std::make_unique<services::TrainingService>(
          *m_database, *m_training, *m_players, *m_availability, *m_session)),
      m_demoData(std::make_unique<services::DemoDataService>(
          *m_database, *m_players, *m_availability, *m_opponents, *m_matches, *m_training, *m_staff,
          *m_auth, *m_session, *m_clock)) {}

ServiceRegistry::~ServiceRegistry() = default;

const services::Clock& ServiceRegistry::clock() const { return *m_clock; }

services::Session& ServiceRegistry::session() { return *m_session; }

services::AuthService& ServiceRegistry::auth() { return *m_auth; }

services::SetupService& ServiceRegistry::setup() { return *m_setup; }

services::SquadService& ServiceRegistry::squad() { return *m_squad; }

services::CalendarService& ServiceRegistry::calendar() { return *m_calendar; }

services::TrainingService& ServiceRegistry::training() { return *m_trainingService; }

services::DemoDataService& ServiceRegistry::demoData() { return *m_demoData; }

} // namespace fdss::viewmodels
