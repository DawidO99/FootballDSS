#include "viewmodels/ServiceRegistry.hpp"

#include "../services/FakeClock.hpp"
#include "data/db/DatabaseManager.hpp"
#include "services/Session.hpp"

#include <QSqlDatabase>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

using namespace fdss;
using viewmodels::RegistryOptions;
using viewmodels::ServiceRegistry;

namespace {

std::unique_ptr<data::DatabaseManager> openDatabase() {
    auto opened = data::DatabaseManager::openInMemory();
    EXPECT_TRUE(opened.has_value()) << opened.error().detail;
    return opened ? std::move(*opened) : nullptr;
}

} // namespace

TEST(ServiceRegistryTest, InstanceIsTheInstalledRegistry) {
    auto registry = ServiceRegistry::create(openDatabase());
    const ServiceRegistry* const expected = registry.get();

    ServiceRegistry::install(std::move(registry));

    EXPECT_EQ(&ServiceRegistry::instance(), expected);
    ServiceRegistry::reset();
}

TEST(ServiceRegistryTest, ResetClosesTheDatabase) {
    const qsizetype before = QSqlDatabase::connectionNames().size();
    ServiceRegistry::install(ServiceRegistry::create(openDatabase()));
    ASSERT_EQ(QSqlDatabase::connectionNames().size(), before + 1);

    ServiceRegistry::reset();

    EXPECT_EQ(QSqlDatabase::connectionNames().size(), before);
}

TEST(ServiceRegistryTest, CanBeInstalledAgainAfterReset) {
    ServiceRegistry::install(ServiceRegistry::create(openDatabase()));
    ServiceRegistry::reset();

    ServiceRegistry::install(ServiceRegistry::create(openDatabase()));

    EXPECT_FALSE(ServiceRegistry::instance().session().user().has_value());
    ServiceRegistry::reset();
}

TEST(ServiceRegistryTest, UsesTheGivenClock) {
    auto clock = std::make_unique<tests::FakeClock>();
    const services::Clock* const expected = clock.get();
    RegistryOptions options;
    options.clock = std::move(clock);

    const auto registry = ServiceRegistry::create(openDatabase(), std::move(options));

    EXPECT_EQ(&registry->clock(), expected);
}

TEST(ServiceRegistryTest, DefaultsToTheSystemClock) {
    const auto registry = ServiceRegistry::create(openDatabase());

    EXPECT_NE(dynamic_cast<const services::SystemClock*>(&registry->clock()), nullptr);
}

TEST(ServiceRegistryTest, UsesTheGivenIterations) {
    RegistryOptions options;
    options.pinIterations = 7;

    const auto registry = ServiceRegistry::create(openDatabase(), std::move(options));

    EXPECT_EQ(registry->auth().hashPin("1234").value_or(core::PinHash{}).iterations, 7);
}

TEST(ServiceRegistryTest, DefaultIterationsAreThoseOfAdr007) {
    EXPECT_EQ(RegistryOptions{}.pinIterations, 120'000);
}
