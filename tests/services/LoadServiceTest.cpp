#include "services/LoadService.hpp"

#include "ServiceFixture.hpp"
#include "core/demo/DemoGenerator.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/DemoDataService.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

using std::chrono::days;
using std::chrono::sys_days;

// FakeClock: today is Sunday 2026-11-01.
constexpr Date kToday{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}};

Date daysAgo(int count) { return Date{sys_days{kToday} - days{count}}; }

class LoadServiceTest : public ServiceTest {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        seedPlayers();
        if (HasFatalFailure()) {
            return;
        }
        ASSERT_TRUE(sql("INSERT INTO opponent (id, name, name_key) VALUES (1, 'Rywal', 'rywal')"));
        m_players = std::make_unique<PlayerRepository>(*m_db);
        m_matches = std::make_unique<MatchRepository>(*m_db);
        m_training = std::make_unique<TrainingRepository>(*m_db);
        m_service =
            std::make_unique<LoadService>(*m_players, *m_training, *m_matches, m_session, m_clock);
    }

    void logInAs(Role role) {
        const StaffUserId id = addUser(std::string(toDbText(role)).c_str(), role);
        logIn(id);
    }

    // A session of team 1 with the given rows.
    void addSession(Date date, int duration, const std::vector<AttendanceDraft>& rows) {
        TrainingSessionDraft draft;
        draft.date = date;
        draft.durationMin = duration;
        const auto id = m_training->create(kTeam, draft);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
        ASSERT_TRUE(m_training->saveAttendance(*id, rows).has_value());
    }

    static AttendanceDraft present(PlayerId player, std::optional<int> rpe,
                                   std::optional<int> minutes = {}) {
        AttendanceDraft draft;
        draft.playerId = player;
        draft.rpe = rpe;
        draft.minutesOverride = minutes;
        return draft;
    }

    MatchId addMatch(Date date, const std::vector<AppearanceDraft>& appearances,
                     MatchStatus status) {
        MatchDraft draft;
        draft.opponentId = kOpponent;
        draft.date = date;
        const auto id = m_matches->create(kTeam, draft);
        EXPECT_TRUE(id.has_value());
        EXPECT_TRUE(m_matches->replaceAppearances(*id, appearances).has_value());
        if (status != MatchStatus::Planned) {
            EXPECT_TRUE(m_matches->setResult(*id, 1, 0, status).has_value());
        }
        return *id;
    }

    static AppearanceDraft played(PlayerId player, bool started, std::optional<int> on, int off,
                                  std::optional<int> rpe) {
        AppearanceDraft draft;
        draft.playerId = player;
        draft.started = started;
        draft.minuteOn = on;
        draft.minuteOff = off;
        draft.rpe = rpe;
        return draft;
    }

    TeamLoad team() const {
        auto future = m_service->teamLoad(kTeam);
        EXPECT_TRUE(future.has_value()) << future.error().detail;
        return future ? future->result() : TeamLoad{};
    }

    PlayerLoadDetail player(PlayerId id) const {
        auto future = m_service->playerLoad(id);
        EXPECT_TRUE(future.has_value()) << future.error().detail;
        return future ? future->result() : PlayerLoadDetail{};
    }

    static const LoadDay* dayOf(const PlayerLoadDetail& detail, Date date) {
        const auto found = std::ranges::find(detail.load.days, date, &LoadDay::date);
        return found != detail.load.days.end() ? &*found : nullptr;
    }

    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<MatchRepository> m_matches;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<LoadService> m_service;
};

} // namespace

TEST_F(LoadServiceTest, NeedsALogin) {
    expectError(m_service->teamLoad(kTeam), ErrorCode::PermissionDenied);
    expectError(m_service->playerLoad(kPlayer), ErrorCode::PermissionDenied);
}

TEST_F(LoadServiceTest, EveryRoleCanRead) {
    for (const Role role : allValues<Role>()) {
        logInAs(role);
        EXPECT_TRUE(m_service->teamLoad(kTeam).has_value()) << toDbText(role);
        EXPECT_TRUE(m_service->playerLoad(kPlayer).has_value()) << toDbText(role);
        m_session.clear();
    }
}

TEST_F(LoadServiceTest, AnUnknownPlayerIsNotFound) {
    logInAs(Role::Coach);
    expectError(m_service->playerLoad(kNoPlayer), ErrorCode::NotFound);
}

TEST_F(LoadServiceTest, ArchivedPlayersAreNotInTheTeamTable) {
    logInAs(Role::Coach);
    ASSERT_TRUE(m_players->archive(kOtherPlayer, daysAgo(3)).has_value());
    addSession(daysAgo(2), 60, {present(kPlayer, 5), present(kOtherPlayer, 6)});

    const TeamLoad load = team();
    ASSERT_EQ(load.players.size(), 1U);
    EXPECT_EQ(load.players.front().player.id, kPlayer);
    EXPECT_EQ(load.today, kToday);
    EXPECT_EQ(load.thresholds, LoadThresholds{});

    // The card of an archived player still shows the history.
    const PlayerLoadDetail archived = player(kOtherPlayer);
    EXPECT_EQ(archived.player.id, kOtherPlayer);
    ASSERT_NE(dayOf(archived, daysAgo(2)), nullptr);
    EXPECT_EQ(dayOf(archived, daysAgo(2))->sessionLoad, 360);
}

TEST_F(LoadServiceTest, SessionsInTheFutureAreIgnored) {
    logInAs(Role::Coach);
    addSession(daysAgo(1), 90, {present(kPlayer, 5)});
    // Saved ahead of time (BACKLOG: nothing forbids it yet).
    addSession(Date{sys_days{kToday} + days{1}}, 90, {present(kPlayer, 9)});

    const PlayerLoadDetail detail = player(kPlayer);
    EXPECT_EQ(detail.load.assessment.indicators.rpeDays, 1);
    EXPECT_EQ(detail.load.days.back().date, daysAgo(1));
    EXPECT_EQ(detail.load.assessment.indicators.weekLoad, 450);
}

TEST_F(LoadServiceTest, SessionsCountTheirMinutesAndOnlyPresentRows) {
    logInAs(Role::Coach);
    addSession(daysAgo(3), 90, {present(kPlayer, 5, 30), present(kOtherPlayer, std::nullopt)});
    AttendanceDraft absent;
    absent.playerId = kOtherPlayer;
    absent.status = AttendanceStatus::Absent;
    addSession(daysAgo(2), 90, {present(kPlayer, 4), absent});

    const PlayerLoadDetail first = player(kPlayer);
    EXPECT_EQ(dayOf(first, daysAgo(3))->sessionLoad, 150);
    EXPECT_EQ(dayOf(first, daysAgo(2))->sessionLoad, 360);

    const PlayerLoadDetail second = player(kOtherPlayer);
    EXPECT_TRUE(second.load.days.empty());
    EXPECT_EQ(second.load.assessment.missingRpeSessions, 1);
    EXPECT_EQ(second.load.assessment.verdict.reasons, std::vector{LoadReason::NoRpe});
}

TEST_F(LoadServiceTest, OnlyFinishedMatchesCount) {
    logInAs(Role::Coach);
    addMatch(daysAgo(7), {played(kPlayer, true, std::nullopt, 90, 8)}, MatchStatus::Finished);
    addMatch(daysAgo(3), {played(kPlayer, true, std::nullopt, 90, 8)}, MatchStatus::Live);
    addMatch(daysAgo(1), {played(kPlayer, true, std::nullopt, 90, 8)}, MatchStatus::Planned);
    addSession(daysAgo(5), 60, {present(kPlayer, 5)});

    const PlayerLoadDetail detail = player(kPlayer);
    EXPECT_EQ(dayOf(detail, daysAgo(7))->matchLoad, 720);
    EXPECT_EQ(dayOf(detail, daysAgo(3))->matchLoad, 0);
    EXPECT_EQ(dayOf(detail, daysAgo(1))->matchLoad, 0);
    EXPECT_EQ(detail.load.assessment.indicators.rpeDays, 2);
}

TEST_F(LoadServiceTest, MatchLoadFromMinutesAndMissingMatchRpe) {
    logInAs(Role::Coach);
    addMatch(daysAgo(2),
             {played(kPlayer, false, 60, 90, 6), played(kOtherPlayer, true, std::nullopt, 90, {})},
             MatchStatus::Finished);
    addSession(daysAgo(2), 60, {present(kPlayer, 3)});

    const PlayerLoadDetail sub = player(kPlayer);
    ASSERT_NE(dayOf(sub, daysAgo(2)), nullptr);
    EXPECT_EQ(dayOf(sub, daysAgo(2))->matchLoad, 6 * 30);
    EXPECT_EQ(dayOf(sub, daysAgo(2))->sessionLoad, 3 * 60);

    const PlayerLoadDetail starter = player(kOtherPlayer);
    EXPECT_TRUE(starter.load.days.empty());
    EXPECT_EQ(starter.load.assessment.missingRpeMatches, 1);
}

TEST_F(LoadServiceTest, TheTeamTableUsesTheSameAssessmentAsThePlayer) {
    logInAs(Role::Coach);
    for (int ago = 40; ago >= 1; ago -= 2) {
        addSession(daysAgo(ago), 60, {present(kPlayer, 5), present(kOtherPlayer, 4)});
    }
    const TeamLoad load = team();
    ASSERT_EQ(load.players.size(), 2U);
    for (const PlayerLoadRow& row : load.players) {
        EXPECT_EQ(row.assessment, player(row.player.id).load.assessment);
        EXPECT_NE(row.assessment.verdict.status, LoadStatus::NotEnoughData);
    }
}

TEST_F(LoadServiceTest, DemoDataShowsTheLoadSpike) {
    auto availability = std::make_unique<AvailabilityRepository>(*m_db);
    auto opponents = std::make_unique<OpponentRepository>(*m_db);
    ASSERT_TRUE(sql("DELETE FROM player"));
    ASSERT_TRUE(sql("DELETE FROM opponent"));
    DemoDataService demo(*m_db, *m_players, *availability, *opponents, *m_matches, *m_training,
                         *m_staff, *m_auth, m_session, m_clock);
    logInAs(Role::Coach);
    ASSERT_TRUE(demo.load(kTeam).has_value());

    const demo::DemoDataset data = demo::generate(demo::kDemoSeed, daysAgo(demo::kDemoDays));
    const auto nameOf = [&](int index) {
        const PlayerDraft& draft = data.players[static_cast<std::size_t>(index)].draft;
        return draft.firstName + " " + draft.lastName;
    };

    const TeamLoad load = team();
    ASSERT_EQ(load.players.size(), static_cast<std::size_t>(demo::kActivePlayers));
    bool spikeIsRed = false;
    for (const PlayerLoadRow& row : load.players) {
        const std::string name = row.player.firstName + " " + row.player.lastName;
        if (name == nameOf(data.story.loadSpike)) {
            spikeIsRed = row.assessment.verdict.status == LoadStatus::High;
        } else if (name != nameOf(data.story.returned)) {
            EXPECT_NE(row.assessment.verdict.status, LoadStatus::High) << name;
        }
    }
    EXPECT_TRUE(spikeIsRed);
}
