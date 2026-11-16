#pragma once

#include "services/AuthService.hpp"
#include "services/Clock.hpp"

#include <memory>

namespace fdss::data {
class AvailabilityRepository;
class ClubRepository;
class DatabaseManager;
class MatchRepository;
class OpponentRepository;
class PlayerRepository;
class StaffRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {
class CalendarService;
class DemoDataService;
class LoadService;
class Session;
class SetupService;
class SquadService;
class TrainingService;
} // namespace fdss::services

namespace fdss::viewmodels {

// At namespace scope: a nested struct with default member initializers cannot be a default argument
// of a function of its enclosing class on GCC and Clang.
struct RegistryOptions {
    // Tests and qml_smoke use a small count.
    int pinIterations = services::kDefaultPinIterations;
    // nullptr: SystemClock.
    std::unique_ptr<services::Clock> clock;
};

// The composition root and the only global state (AGENTS section 5). Owns the database, the
// repositories and the services. The data types are only declared here, so a viewmodel can reach
// the services and nothing below them. A new service is one member, one accessor and one
// construction line in this class; main.cpp and qml_smoke only call create().
class ServiceRegistry {
    // Lets create() use make_unique while keeping the constructor out of the public interface.
    struct PassKey {
        explicit PassKey() = default;
    };

public:
    [[nodiscard]] static std::unique_ptr<ServiceRegistry>
    create(std::unique_ptr<data::DatabaseManager> database, RegistryOptions options = {});

    // Before any QML that uses a viewmodel loads. A second install without reset() is fatal.
    static void install(std::unique_ptr<ServiceRegistry> registry);
    // Fatal when nothing is installed.
    [[nodiscard]] static ServiceRegistry& instance();
    // Closes the database. Viewmodels keep references to the services, so destroy the QML engine
    // first. main.cpp calls it too: static destruction would run after QGuiApplication is gone.
    static void reset();

    ServiceRegistry(PassKey, std::unique_ptr<data::DatabaseManager> database,
                    RegistryOptions options);
    ~ServiceRegistry();
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    [[nodiscard]] const services::Clock& clock() const;
    [[nodiscard]] services::Session& session();
    [[nodiscard]] services::AuthService& auth();
    [[nodiscard]] services::SetupService& setup();
    [[nodiscard]] services::SquadService& squad();
    [[nodiscard]] services::CalendarService& calendar();
    [[nodiscard]] services::TrainingService& training();
    [[nodiscard]] services::DemoDataService& demoData();
    [[nodiscard]] services::LoadService& load();

private:
    // Destroyed from the bottom up, the database last.
    std::unique_ptr<data::DatabaseManager> m_database;
    std::unique_ptr<services::Clock> m_clock;
    std::unique_ptr<data::ClubRepository> m_clubs;
    std::unique_ptr<data::StaffRepository> m_staff;
    std::unique_ptr<data::PlayerRepository> m_players;
    std::unique_ptr<data::AvailabilityRepository> m_availability;
    std::unique_ptr<data::OpponentRepository> m_opponents;
    std::unique_ptr<data::MatchRepository> m_matches;
    std::unique_ptr<data::TrainingRepository> m_training;
    std::unique_ptr<services::Session> m_session;
    std::unique_ptr<services::AuthService> m_auth;
    std::unique_ptr<services::SetupService> m_setup;
    std::unique_ptr<services::SquadService> m_squad;
    std::unique_ptr<services::CalendarService> m_calendar;
    std::unique_ptr<services::TrainingService> m_trainingService;
    std::unique_ptr<services::DemoDataService> m_demoData;
    std::unique_ptr<services::LoadService> m_load;
};

} // namespace fdss::viewmodels
