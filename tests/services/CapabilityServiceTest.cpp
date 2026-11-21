#include "services/CapabilityService.hpp"

#include "ServiceFixture.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/CapabilityRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/DemoDataService.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

class CapabilityServiceTest : public ServiceTest {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        seedTeams();
        m_counts = std::make_unique<CapabilityRepository>(*m_db);
        m_service = std::make_unique<CapabilityService>(*m_counts, m_session, m_clock);
        m_coach = addUser("Jan Trener", Role::Coach);
    }

    Capability capability(CapabilityKind kind, bool showAdvanced = false) const {
        const auto all = m_service->evaluate(kTeam, showAdvanced);
        EXPECT_TRUE(all.has_value()) << (all ? "" : all.error().detail);
        return all ? all->at(static_cast<std::size_t>(kind)) : Capability{};
    }

    void loadDemo() {
        PlayerRepository players(*m_db);
        AvailabilityRepository availability(*m_db);
        OpponentRepository opponents(*m_db);
        MatchRepository matches(*m_db);
        TrainingRepository training(*m_db);
        DemoDataService demo(*m_db, players, availability, opponents, matches, training, *m_staff,
                             *m_auth, m_session, m_clock);
        const auto loaded = demo.load(kTeam);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().detail;
    }

    std::unique_ptr<CapabilityRepository> m_counts;
    std::unique_ptr<CapabilityService> m_service;
    StaffUserId m_coach;
};

} // namespace

TEST_F(CapabilityServiceTest, NeedsALoggedInUser) {
    expectError(m_service->evaluate(kTeam, false), ErrorCode::PermissionDenied);
}

TEST_F(CapabilityServiceTest, EmptyClubNeedsElevenPlayersAndFourteenRpeDays) {
    logIn(m_coach);
    const Capability squad = capability(CapabilityKind::Squad);
    EXPECT_EQ(squad.state, CapabilityState::Locked);
    EXPECT_EQ(squad.missing(), 11);
    const Capability load = capability(CapabilityKind::Load);
    EXPECT_EQ(load.state, CapabilityState::Locked);
    EXPECT_EQ(load.missing(), 14);
    EXPECT_EQ(capability(CapabilityKind::Matches).state, CapabilityState::Locked);
    EXPECT_EQ(capability(CapabilityKind::EventData).state, CapabilityState::Hidden);
    EXPECT_EQ(capability(CapabilityKind::EventData, true).state, CapabilityState::Locked);
}

TEST_F(CapabilityServiceTest, SquadUnlocksWithTheEleventhPlayer) {
    logIn(m_coach);
    for (int i = 1; i <= 10; ++i) {
        ASSERT_TRUE(sql("INSERT INTO player (team_id, first_name, last_name, primary_position) "
                        "VALUES (1, 'Jan', ?, 'cm')",
                        {QStringLiteral("Gracz%1").arg(i)}));
    }
    EXPECT_EQ(capability(CapabilityKind::Squad).missing(), 1);
    ASSERT_TRUE(sql("INSERT INTO player (team_id, first_name, last_name, primary_position) "
                    "VALUES (1, 'Jan', 'Gracz11', 'gk')"));
    EXPECT_EQ(capability(CapabilityKind::Squad).state, CapabilityState::Available);
}

TEST_F(CapabilityServiceTest, DemoDataMakesEverythingAvailable) {
    logIn(m_coach);
    loadDemo();
    const auto all = m_service->evaluate(kTeam, false);
    ASSERT_TRUE(all.has_value()) << all.error().detail;
    for (const Capability& capability : *all) {
        if (capability.kind == CapabilityKind::EventData) {
            // No StatsBomb file in Team A: the Pro module stays hidden.
            EXPECT_EQ(capability.state, CapabilityState::Hidden);
        } else {
            EXPECT_EQ(capability.state, CapabilityState::Available)
                << static_cast<int>(capability.kind);
        }
    }
    EXPECT_EQ(capability(CapabilityKind::Load).daysToFullLoad, std::nullopt);
}
