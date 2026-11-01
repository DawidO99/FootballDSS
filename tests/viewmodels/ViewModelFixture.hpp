#pragma once

// Installs a ServiceRegistry on a migrated in-memory database for the viewmodel tests.

#include "../services/FakeClock.hpp"
#include "core/domain/Club.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/domain/Team.hpp"
#include "data/db/DatabaseManager.hpp"
#include "services/AuthService.hpp"
#include "services/SetupService.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QObject>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace fdss::tests {

// How many times a signal was emitted, without linking Qt Test for QSignalSpy.
class SignalCount {
public:
    template <class Sender, class Signal> SignalCount(const Sender* sender, Signal signal) {
        QObject::connect(sender, signal, &m_context, [this] { ++m_count; });
    }

    [[nodiscard]] int count() const { return m_count; }

private:
    QObject m_context;
    int m_count = 0;
};

class ViewModelTest : public ::testing::Test {
protected:
    static constexpr int kIterations = 4;
    static constexpr const char* kPin = "1234";

    void SetUp() override {
        auto opened = data::DatabaseManager::openInMemory();
        ASSERT_TRUE(opened.has_value()) << opened.error().detail;
        m_db = opened->get();

        viewmodels::RegistryOptions options;
        options.pinIterations = kIterations;
        auto clock = std::make_unique<FakeClock>();
        m_clock = clock.get();
        options.clock = std::move(clock);
        viewmodels::ServiceRegistry::install(
            viewmodels::ServiceRegistry::create(std::move(*opened), std::move(options)));
    }

    void TearDown() override { viewmodels::ServiceRegistry::reset(); }

    [[nodiscard]] static viewmodels::ServiceRegistry& registry() {
        return viewmodels::ServiceRegistry::instance();
    }

    // Club, team and Coach; the Coach stays logged in.
    static core::StaffUser completeSetup() {
        core::ClubDraft club;
        club.name = "KS Test";
        core::TeamDraft team;
        team.name = "Seniorzy";
        team.season = "2026/27";
        const auto user = registry().setup().completeSetup(club, team, "Jan Trener", kPin);
        EXPECT_TRUE(user.has_value()) << user.error().detail;
        return user.value_or(core::StaffUser{});
    }

    // Needs a logged-in Coach.
    static core::StaffUserId addStaff(const char* name, core::Role role) {
        core::StaffUserDraft draft;
        draft.name = name;
        draft.role = role;
        const auto id = registry().auth().addStaff(draft, kPin);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(core::StaffUserId{});
    }

    // Owned by the registry; for the tests that break the database on purpose.
    data::DatabaseManager* m_db = nullptr;
    // Owned by the registry; for the tests that move time.
    FakeClock* m_clock = nullptr;
};

} // namespace fdss::tests
