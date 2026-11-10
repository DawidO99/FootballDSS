#include "services/DemoDataService.hpp"

#include "ServiceFixture.hpp"
#include "core/demo/DemoGenerator.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

class DemoDataServiceTest : public ServiceTest {
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
        m_demo = std::make_unique<DemoDataService>(*m_db, *m_players, *m_availability, *m_opponents,
                                                   *m_matches, *m_training, *m_staff, *m_auth,
                                                   m_session, m_clock);
        m_coach = addUser("Jan Trener", Role::Coach);
    }

    void expectNoDemoData() const {
        EXPECT_EQ(count("SELECT count(*) FROM player"), 0);
        EXPECT_EQ(count("SELECT count(*) FROM availability"), 0);
        EXPECT_EQ(count("SELECT count(*) FROM match"), 0);
        EXPECT_EQ(count("SELECT count(*) FROM training_session"), 0);
        EXPECT_EQ(count("SELECT count(*) FROM staff_user"), 1);
    }

    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<AvailabilityRepository> m_availability;
    std::unique_ptr<OpponentRepository> m_opponents;
    std::unique_ptr<MatchRepository> m_matches;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<DemoDataService> m_demo;
    StaffUserId m_coach;
};

} // namespace

TEST_F(DemoDataServiceTest, CoachLoadsAHalfSeason) {
    logIn(m_coach);
    const auto summary = m_demo->load(kTeam);
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->players, 24);
    EXPECT_EQ(summary->matches, 13);
    EXPECT_EQ(summary->sessions, 40);

    EXPECT_EQ(count("SELECT count(*) FROM player WHERE team_id = 1 AND archived_at IS NULL"), 22);
    EXPECT_EQ(count("SELECT count(*) FROM player WHERE archived_at IS NOT NULL"), 2);
    EXPECT_EQ(count("SELECT count(*) FROM player "
                    "WHERE archived_at IS NULL AND primary_position = 'gk'"),
              2);
    EXPECT_EQ(count("SELECT count(*) FROM opponent"), 8);
    EXPECT_EQ(count("SELECT count(*) FROM match WHERE status = 'finished' "
                    "AND goals_for IS NOT NULL AND competition = 'B-klasa'"),
              12);
    EXPECT_EQ(count("SELECT count(*) FROM match WHERE status = 'planned' AND date >= ?",
                    {QStringLiteral("2026-11-01")}),
              1);
    EXPECT_EQ(count("SELECT count(*) FROM training_session WHERE team_id = 1"), 40);
    EXPECT_GT(count("SELECT count(*) FROM attendance WHERE status = 'present'"), 500);
    EXPECT_EQ(count("SELECT count(*) FROM attendance a JOIN training_session s "
                    "ON s.id = a.session_id WHERE s.date >= '2026-11-01'"),
              0);
    EXPECT_GT(count("SELECT count(*) FROM appearance"), 12 * 13);
    EXPECT_EQ(count("SELECT count(*) FROM match_event WHERE type = 'red'"), 1);
    // Every finished match: the goals in the result are the goal events.
    EXPECT_EQ(count("SELECT count(*) FROM match m WHERE status = 'finished' AND goals_for <> "
                    "(SELECT count(*) FROM match_event e WHERE e.match_id = m.id "
                    "AND e.type = 'goal')"),
              0);
    EXPECT_EQ(count("SELECT count(*) FROM staff_user WHERE role IN ('assistant', 'physio')"), 2);
    EXPECT_EQ(count("SELECT count(*) FROM availability"), 5);
}

TEST_F(DemoDataServiceTest, TodayShowsTwoInjuriesAndASuspension) {
    logIn(m_coach);
    ASSERT_TRUE(m_demo->load(kTeam).has_value());
    const auto active = m_availability->listActiveOn(kTeam, m_clock.today());
    ASSERT_TRUE(active.has_value()) << active.error().detail;
    int injuries = 0;
    int suspensions = 0;
    for (const Availability& entry : *active) {
        injuries += entry.type == AvailabilityType::Injury ? 1 : 0;
        suspensions += entry.type == AvailabilityType::Suspension ? 1 : 0;
    }
    EXPECT_EQ(injuries, 2);
    EXPECT_EQ(suspensions, 1);
}

TEST_F(DemoDataServiceTest, DemoStaffLogsInWithTheDemoPin) {
    logIn(m_coach);
    ASSERT_TRUE(m_demo->load(kTeam).has_value());
    const auto staff = m_staff->listActive();
    ASSERT_TRUE(staff.has_value());
    int checked = 0;
    for (const StaffUser& user : *staff) {
        if (user.role == Role::Coach) {
            continue;
        }
        ++checked;
        const auto check = m_auth->login(user.id, std::string(demo::kDemoStaffPin));
        ASSERT_TRUE(check.has_value()) << check.error().detail;
        EXPECT_EQ(check->status, PinCheckStatus::Accepted) << user.name;
    }
    EXPECT_EQ(checked, 2);
}

TEST_F(DemoDataServiceTest, SecondLoadIsAConflict) {
    logIn(m_coach);
    ASSERT_TRUE(m_demo->load(kTeam).has_value());
    expectError(m_demo->load(kTeam), ErrorCode::Conflict);
    EXPECT_EQ(count("SELECT count(*) FROM player"), 24);
    EXPECT_EQ(count("SELECT count(*) FROM staff_user"), 3);
}

TEST_F(DemoDataServiceTest, NeedsALoggedInCoach) {
    expectError(m_demo->load(kTeam), ErrorCode::PermissionDenied);
    const StaffUserId assistant = addUser("Asystent", Role::Assistant);
    logIn(assistant);
    expectError(m_demo->load(kTeam), ErrorCode::PermissionDenied);
    EXPECT_EQ(count("SELECT count(*) FROM player"), 0);
}

TEST_F(DemoDataServiceTest, FailureWritesNothing) {
    // An opponent of the demo already exists: the unique name stops the load half-way.
    const Date start{std::chrono::sys_days{m_clock.today()} - std::chrono::days{demo::kDemoDays}};
    const demo::DemoDataset data = demo::generate(demo::kDemoSeed, start);
    ASSERT_TRUE(m_opponents->create(data.opponents.back()).has_value());

    logIn(m_coach);
    EXPECT_FALSE(m_demo->load(kTeam).has_value());
    expectNoDemoData();
    EXPECT_EQ(count("SELECT count(*) FROM opponent"), 1);
}

TEST_F(DemoDataServiceTest, ALateFailureRollsBackEverything) {
    // The Physio is the last row the demo writes: matches, appearances, events, sessions and
    // attendance are all in by then.
    ASSERT_TRUE(sql("CREATE TEMP TRIGGER fail_late BEFORE INSERT ON staff_user "
                    "WHEN NEW.role = 'physio' BEGIN SELECT RAISE(ABORT, 'late failure'); END"));
    logIn(m_coach);

    const auto result = m_demo->load(kTeam);

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().detail.find("late failure"), std::string::npos)
        << result.error().detail;
    expectNoDemoData();
    EXPECT_EQ(count("SELECT count(*) FROM opponent"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM appearance"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM match_event"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM attendance"), 0);

    // Nothing is left half-open: the next attempt works once the cause is gone.
    ASSERT_TRUE(sql("DROP TRIGGER fail_late"));
    EXPECT_TRUE(m_demo->load(kTeam).has_value());
}
