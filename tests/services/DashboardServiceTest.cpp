#include "services/DashboardService.hpp"

#include "ServiceFixture.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/LineupRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/DemoDataService.hpp"
#include "services/LineupService.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

constexpr int kThreshold = 4;

class DashboardServiceTest : public ServiceTest {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        seedTeams();
        m_players = std::make_unique<PlayerRepository>(*m_db);
        m_availability = std::make_unique<AvailabilityRepository>(*m_db);
        m_opponents = std::make_unique<OpponentRepository>(*m_db);
        m_matches = std::make_unique<MatchRepository>(*m_db);
        m_training = std::make_unique<TrainingRepository>(*m_db);
        m_lineups = std::make_unique<LineupRepository>(*m_db);
        m_lineup =
            std::make_unique<LineupService>(*m_db, *m_lineups, *m_matches, *m_opponents, *m_players,
                                            *m_availability, *m_training, m_session, m_clock);
        m_service = std::make_unique<DashboardService>(*m_matches, *m_opponents, *m_players,
                                                       *m_availability, *m_training, *m_lineup,
                                                       m_session, m_clock);
        m_coach = addUser("Jan Trener", Role::Coach);
    }

    void loadDemo() {
        DemoDataService demo(*m_db, *m_players, *m_availability, *m_opponents, *m_matches,
                             *m_training, *m_staff, *m_auth, m_session, m_clock);
        const auto loaded = demo.load(kTeam);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().detail;
    }

    void addOpponent() {
        ASSERT_TRUE(sql("INSERT INTO opponent (id, name, name_key) VALUES (1, 'Rywal', 'rywal')"));
    }

    DashboardOverview overview() const {
        auto result = m_service->overview(kTeam, kThreshold);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().detail);
        return result.value_or(DashboardOverview{});
    }

    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<AvailabilityRepository> m_availability;
    std::unique_ptr<OpponentRepository> m_opponents;
    std::unique_ptr<MatchRepository> m_matches;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<LineupRepository> m_lineups;
    std::unique_ptr<LineupService> m_lineup;
    std::unique_ptr<DashboardService> m_service;
    StaffUserId m_coach;
};

int countOf(const DashboardOverview& overview, AvailabilityType type) {
    return static_cast<int>(std::ranges::count(
        overview.unavailable, type, [](const UnavailablePlayer& row) { return row.entry.type; }));
}

} // namespace

TEST_F(DashboardServiceTest, NeedsALoggedInUser) {
    expectError(m_service->overview(kTeam, kThreshold), ErrorCode::PermissionDenied);
}

TEST_F(DashboardServiceTest, EmptyClubHasNothingToShow) {
    logIn(m_coach);
    const DashboardOverview result = overview();
    EXPECT_EQ(result.today, m_clock.today());
    EXPECT_FALSE(result.nextMatch.has_value());
    EXPECT_TRUE(result.unavailable.empty());
    EXPECT_TRUE(result.lastMatches.empty());
    EXPECT_TRUE(result.todaySessions.empty());
}

TEST_F(DashboardServiceTest, DemoClubShowsNextMatchAbsencesAndForm) {
    logIn(m_coach);
    loadDemo();
    const DashboardOverview result = overview();

    ASSERT_TRUE(result.nextMatch.has_value());
    EXPECT_EQ(result.nextMatch->match.match.status, MatchStatus::Planned);
    EXPECT_GE(result.nextMatch->daysToGo, 0);
    EXPECT_FALSE(result.nextMatch->match.opponentName.empty());
    ASSERT_TRUE(result.nextMatch->lineup.has_value());

    EXPECT_EQ(countOf(result, AvailabilityType::Injury), 2);
    EXPECT_EQ(countOf(result, AvailabilityType::Suspension), 1);
    // The coach reads the descriptions.
    EXPECT_TRUE(std::ranges::any_of(result.unavailable, [](const UnavailablePlayer& row) {
        return row.entry.description.has_value();
    }));

    ASSERT_EQ(result.lastMatches.size(), static_cast<std::size_t>(DashboardService::kFormMatches));
    for (std::size_t i = 1; i < result.lastMatches.size(); ++i) {
        EXPECT_GE(result.lastMatches[i - 1].match.match.date,
                  result.lastMatches[i].match.match.date);
    }
    for (const FormMatch& form : result.lastMatches) {
        EXPECT_EQ(form.match.match.status, MatchStatus::Finished);
        ASSERT_TRUE(form.averageRating.has_value());
        EXPECT_GE(*form.averageRating, 1.0);
        EXPECT_LE(*form.averageRating, 10.0);
    }
}

TEST_F(DashboardServiceTest, AssistantDoesNotReadAvailabilityDescriptions) {
    logIn(m_coach);
    loadDemo();
    logIn(addUser("Anna Asystent", Role::Assistant));
    const DashboardOverview result = overview();
    ASSERT_FALSE(result.unavailable.empty());
    for (const UnavailablePlayer& row : result.unavailable) {
        EXPECT_FALSE(row.entry.description.has_value());
    }
}

TEST_F(DashboardServiceTest, NextMatchIsLiveOrFirstPlannedFromToday) {
    logIn(m_coach);
    addOpponent();
    // Today is 2026-11-01. A forgotten planned match before today is skipped.
    ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue) "
                    "VALUES (1, 1, 1, '2026-10-25', 'home'), (2, 1, 1, '2026-11-04', 'away'), "
                    "(3, 1, 1, '2026-11-11', 'home')"));
    DashboardOverview result = overview();
    ASSERT_TRUE(result.nextMatch.has_value());
    EXPECT_EQ(result.nextMatch->match.match.id, MatchId{2});
    EXPECT_EQ(result.nextMatch->daysToGo, 3);
    EXPECT_EQ(result.nextMatch->match.opponentName, "Rywal");
    ASSERT_TRUE(result.nextMatch->lineup.has_value());
    EXPECT_FALSE(result.nextMatch->lineup->saved);

    ASSERT_TRUE(sql("UPDATE match SET status = 'live', goals_for = 0, goals_against = 0 "
                    "WHERE id = 1"));
    result = overview();
    ASSERT_TRUE(result.nextMatch.has_value());
    EXPECT_EQ(result.nextMatch->match.match.id, MatchId{1});
    EXPECT_EQ(result.nextMatch->daysToGo, -7);
    EXPECT_FALSE(result.nextMatch->lineup.has_value());
}

TEST_F(DashboardServiceTest, FormWithoutRatingsAndTodaysSessions) {
    logIn(m_coach);
    addOpponent();
    ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, status, goals_for, "
                    "goals_against) VALUES (1, 1, 1, '2026-10-25', 'home', 'finished', 1, 1)"));
    ASSERT_TRUE(sql("INSERT INTO training_session (team_id, date, start_time, duration_min, type) "
                    "VALUES (1, '2026-11-01', '18:00', 90, 'training'), "
                    "(1, '2026-11-02', '18:00', 90, 'training')"));
    const DashboardOverview result = overview();
    ASSERT_EQ(result.lastMatches.size(), 1U);
    EXPECT_FALSE(result.lastMatches.front().averageRating.has_value());
    ASSERT_EQ(result.todaySessions.size(), 1U);
    EXPECT_EQ(result.todaySessions.front().date, m_clock.today());
}
