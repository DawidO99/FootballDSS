#include "viewmodels/ServiceRegistry.hpp"

#include "data/db/DatabaseManager.hpp"
#include "data/repositories/ClubRepository.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "services/Session.hpp"
#include "services/SetupService.hpp"

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
      m_session(std::make_unique<services::Session>()),
      m_auth(std::make_unique<services::AuthService>(*m_database, *m_staff, *m_session, *m_clock,
                                                     options.pinIterations)),
      m_setup(std::make_unique<services::SetupService>(*m_database, *m_clubs, *m_staff, *m_auth,
                                                       *m_session)) {}

ServiceRegistry::~ServiceRegistry() = default;

const services::Clock& ServiceRegistry::clock() const { return *m_clock; }

services::Session& ServiceRegistry::session() { return *m_session; }

services::AuthService& ServiceRegistry::auth() { return *m_auth; }

services::SetupService& ServiceRegistry::setup() { return *m_setup; }

} // namespace fdss::viewmodels
