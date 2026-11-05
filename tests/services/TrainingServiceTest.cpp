#include "services/TrainingService.hpp"

#include "ServiceFixture.hpp"
#include "core/domain/Permissions.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"

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

// The date of session 1 of the fixture.
constexpr Date kSessionDay{std::chrono::year{2026}, std::chrono::November, std::chrono::day{2}};
constexpr Date kDayBefore{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}};
constexpr Date kDayAfter{std::chrono::year{2026}, std::chrono::November, std::chrono::day{3}};

AttendanceDraft present(PlayerId player, std::optional<int> rpe = {},
                        std::optional<int> minutes = {}) {
    AttendanceDraft draft;
    draft.playerId = player;
    draft.rpe = rpe;
    draft.minutesOverride = minutes;
    return draft;
}

AttendanceDraft withStatus(PlayerId player, AttendanceStatus status) {
    AttendanceDraft draft;
    draft.playerId = player;
    draft.status = status;
    return draft;
}

class TrainingServiceTest : public ServiceTest {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        seedCalendar();
        if (HasFatalFailure()) {
            return;
        }
        m_players = std::make_unique<PlayerRepository>(*m_db);
        m_availability = std::make_unique<AvailabilityRepository>(*m_db);
        m_training = std::make_unique<TrainingRepository>(*m_db);
        m_service = std::make_unique<TrainingService>(*m_db, *m_training, *m_players,
                                                      *m_availability, m_session);
    }

    void logInAs(Role role) {
        const StaffUserId id = addUser(std::string(toDbText(role)).c_str(), role);
        logIn(id);
    }

    void addAvailability(PlayerId player, AvailabilityType type, Date from,
                         std::optional<Date> expectedTo = {}, std::optional<Date> actualTo = {}) {
        AvailabilityDraft draft;
        draft.type = type;
        draft.fromDate = from;
        draft.expectedTo = expectedTo;
        draft.actualTo = actualTo;
        const auto id = m_availability->create(player, draft);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
    }

    [[nodiscard]] AttendanceSheetRow sheetRow(PlayerId player) const {
        const auto sheet = m_service->sheet(kSession);
        EXPECT_TRUE(sheet.has_value());
        if (sheet) {
            const auto found = std::ranges::find_if(
                sheet->rows, [&](const auto& row) { return row.player.id == player; });
            if (found != sheet->rows.end()) {
                return *found;
            }
        }
        ADD_FAILURE() << "no row for the player";
        return {};
    }

    [[nodiscard]] std::optional<Attendance> storedRow(PlayerId player) const {
        const auto rows = m_training->listAttendance(kSession);
        EXPECT_TRUE(rows.has_value());
        if (rows) {
            const auto found = std::ranges::find_if(
                *rows, [&](const Attendance& row) { return row.playerId == player; });
            if (found != rows->end()) {
                return *found;
            }
        }
        return std::nullopt;
    }

    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<AvailabilityRepository> m_availability;
    std::unique_ptr<TrainingRepository> m_training;
    std::unique_ptr<TrainingService> m_service;
};

TEST_F(TrainingServiceTest, ReadingNeedsALogin) {
    expectError(m_service->listSessions(kTeam), ErrorCode::PermissionDenied);
    expectError(m_service->sheet(kSession), ErrorCode::PermissionDenied);
}

TEST_F(TrainingServiceTest, EveryRoleMayRead) {
    for (const Role role : allValues<Role>()) {
        logInAs(role);
        EXPECT_TRUE(m_service->listSessions(kTeam).has_value()) << toDbText(role);
        EXPECT_TRUE(m_service->sheet(kSession).has_value()) << toDbText(role);
        m_session.clear();
    }
}

TEST_F(TrainingServiceTest, SavingFollowsTheRoleMatrix) {
    const std::vector<AttendanceDraft> rows{present(kPlayer, 5)};
    for (const Role role : allValues<Role>()) {
        logInAs(role);
        const auto saved = m_service->saveAttendance(kSession, rows);
        if (isAllowed(role, Permission::EditTraining)) {
            ASSERT_TRUE(saved.has_value()) << toDbText(role);
            EXPECT_TRUE(saved->empty()) << toDbText(role);
        } else {
            expectError(saved, ErrorCode::PermissionDenied);
        }
        m_session.clear();
    }
}

TEST_F(TrainingServiceTest, AnalystChangesNothing) {
    logInAs(Role::Analyst);
    const std::vector<AttendanceDraft> rows{present(kPlayer, 5)};
    expectError(m_service->saveAttendance(kSession, rows), ErrorCode::PermissionDenied);
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance"), 0);
}

TEST_F(TrainingServiceTest, SessionsComeNewestFirst) {
    logInAs(Role::Coach);
    ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                    "VALUES (2, 1, '2026-11-05', 90, 'training')"));
    ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                    "VALUES (3, 1, '2026-10-20', 90, 'gym')"));
    ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                    "VALUES (4, 2, '2026-12-01', 90, 'gym')"));
    const auto sessions = m_service->listSessions(kTeam);
    ASSERT_TRUE(sessions.has_value());
    ASSERT_EQ(sessions->size(), 3U);
    EXPECT_EQ((*sessions)[0].id, TrainingSessionId{2});
    EXPECT_EQ((*sessions)[1].id, TrainingSessionId{1});
    EXPECT_EQ((*sessions)[2].id, TrainingSessionId{3});
}

TEST_F(TrainingServiceTest, MissingSessionIsNotFound) {
    logInAs(Role::Coach);
    expectError(m_service->sheet(TrainingSessionId{999}), ErrorCode::NotFound);
    const std::vector<AttendanceDraft> rows{present(kPlayer)};
    expectError(m_service->saveAttendance(TrainingSessionId{999}, rows), ErrorCode::NotFound);
}

TEST_F(TrainingServiceTest, SheetLeavesOutArchivedPlayers) {
    logInAs(Role::Coach);
    ASSERT_TRUE(m_players->archive(kOtherPlayer, kDayBefore).has_value());
    const auto sheet = m_service->sheet(kSession);
    ASSERT_TRUE(sheet.has_value());
    ASSERT_EQ(sheet->rows.size(), 1U);
    EXPECT_EQ(sheet->rows[0].player.id, kPlayer);
    EXPECT_EQ(sheet->session.durationMin, 90);
}

TEST_F(TrainingServiceTest, DefaultsToPresentWithNothingFilledIn) {
    logInAs(Role::Coach);
    const AttendanceSheetRow row = sheetRow(kPlayer);
    EXPECT_FALSE(row.saved);
    EXPECT_EQ(row.draft.playerId, kPlayer);
    EXPECT_EQ(row.draft.status, AttendanceStatus::Present);
    EXPECT_EQ(row.defaultStatus, AttendanceStatus::Present);
    EXPECT_FALSE(row.draft.rpe.has_value());
    EXPECT_FALSE(row.draft.minutesOverride.has_value());
}

TEST_F(TrainingServiceTest, InjuryAndIllnessDefaultToInjured) {
    logInAs(Role::Coach);
    addAvailability(kPlayer, AvailabilityType::Injury, kDayBefore);
    addAvailability(kOtherPlayer, AvailabilityType::Illness, kSessionDay);
    EXPECT_EQ(sheetRow(kPlayer).draft.status, AttendanceStatus::Injured);
    EXPECT_EQ(sheetRow(kOtherPlayer).draft.status, AttendanceStatus::Injured);
}

TEST_F(TrainingServiceTest, AbsenceDefaultsToExcused) {
    logInAs(Role::Coach);
    addAvailability(kPlayer, AvailabilityType::Absence, kSessionDay, kSessionDay);
    EXPECT_EQ(sheetRow(kPlayer).draft.status, AttendanceStatus::Excused);
    EXPECT_EQ(sheetRow(kPlayer).defaultStatus, AttendanceStatus::Excused);
}

TEST_F(TrainingServiceTest, SuspensionDefaultsToPresent) {
    logInAs(Role::Coach);
    addAvailability(kPlayer, AvailabilityType::Suspension, kSessionDay, kSessionDay);
    EXPECT_EQ(sheetRow(kPlayer).draft.status, AttendanceStatus::Present);
}

TEST_F(TrainingServiceTest, OverlappingEntriesInjuryThenAbsence) {
    logInAs(Role::Coach);
    addAvailability(kPlayer, AvailabilityType::Absence, kSessionDay, kSessionDay);
    addAvailability(kPlayer, AvailabilityType::Suspension, kSessionDay, kSessionDay);
    EXPECT_EQ(sheetRow(kPlayer).draft.status, AttendanceStatus::Excused);
    addAvailability(kPlayer, AvailabilityType::Injury, kSessionDay);
    EXPECT_EQ(sheetRow(kPlayer).draft.status, AttendanceStatus::Injured);
}

TEST_F(TrainingServiceTest, AvailabilityCountsOnTheDateOfTheSession) {
    logInAs(Role::Coach);
    // Injured from the day after the session on.
    addAvailability(kPlayer, AvailabilityType::Injury, kDayAfter);
    // Back on the day of the session: actualTo is the first available day.
    addAvailability(kOtherPlayer, AvailabilityType::Injury, kDayBefore, {}, kSessionDay);
    EXPECT_EQ(sheetRow(kPlayer).draft.status, AttendanceStatus::Present);
    EXPECT_EQ(sheetRow(kOtherPlayer).draft.status, AttendanceStatus::Present);
}

TEST_F(TrainingServiceTest, SavedRowWinsOverTheDefault) {
    logInAs(Role::Coach);
    addAvailability(kPlayer, AvailabilityType::Injury, kDayBefore);
    const std::vector<AttendanceDraft> rows{present(kPlayer, 3, 45)};
    ASSERT_TRUE(m_service->saveAttendance(kSession, rows).has_value());
    const AttendanceSheetRow row = sheetRow(kPlayer);
    EXPECT_TRUE(row.saved);
    EXPECT_EQ(row.draft.status, AttendanceStatus::Present);
    EXPECT_EQ(row.draft.rpe, 3);
    EXPECT_EQ(row.draft.minutesOverride, 45);
    EXPECT_EQ(row.defaultStatus, AttendanceStatus::Injured);
}

TEST_F(TrainingServiceTest, ValuesAtTheLimitsAreSaved) {
    logInAs(Role::Coach);
    const std::vector<AttendanceDraft> rows{present(kPlayer, 0, 0), present(kOtherPlayer, 10, 240)};
    const auto saved = m_service->saveAttendance(kSession, rows);
    ASSERT_TRUE(saved.has_value()) << saved.error().detail;
    EXPECT_TRUE(saved->empty());
    EXPECT_EQ(storedRow(kPlayer)->rpe, 0);
    EXPECT_EQ(storedRow(kOtherPlayer)->minutesOverride, 240);
}

TEST_F(TrainingServiceTest, EachBadRowIsReportedAndNothingIsWritten) {
    logInAs(Role::Coach);
    const std::vector<AttendanceDraft> before{present(kPlayer, 5)};
    ASSERT_TRUE(m_service->saveAttendance(kSession, before).has_value());

    const std::vector<AttendanceDraft> rows{present(kPlayer, 11), present(kOtherPlayer, 5, 241)};
    const auto saved = m_service->saveAttendance(kSession, rows);
    ASSERT_TRUE(saved.has_value());
    ASSERT_EQ(saved->size(), 2U);
    EXPECT_EQ((*saved)[0].player, kPlayer);
    EXPECT_EQ((*saved)[0].error.field, "rpe");
    EXPECT_EQ((*saved)[0].error.code, ErrorCode::InvalidInput);
    EXPECT_EQ((*saved)[1].player, kOtherPlayer);
    EXPECT_EQ((*saved)[1].error.field, "minutes_override");

    EXPECT_EQ(storedRow(kPlayer)->rpe, 5);
    EXPECT_FALSE(storedRow(kOtherPlayer).has_value());
}

TEST_F(TrainingServiceTest, NegativeValuesAreRefused) {
    logInAs(Role::Coach);
    const std::vector<AttendanceDraft> rows{present(kPlayer, -1), present(kOtherPlayer, 5, -1)};
    const auto saved = m_service->saveAttendance(kSession, rows);
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->size(), 2U);
}

TEST_F(TrainingServiceTest, RpeAndMinutesMustBeEmptyUnlessPresent) {
    logInAs(Role::Coach);
    for (const AttendanceStatus status :
         {AttendanceStatus::Absent, AttendanceStatus::Excused, AttendanceStatus::Injured}) {
        AttendanceDraft withRpe = withStatus(kPlayer, status);
        withRpe.rpe = 5;
        AttendanceDraft withMinutes = withStatus(kOtherPlayer, status);
        withMinutes.minutesOverride = 30;
        const std::vector<AttendanceDraft> rows{withRpe, withMinutes};
        const auto saved = m_service->saveAttendance(kSession, rows);
        ASSERT_TRUE(saved.has_value());
        ASSERT_EQ(saved->size(), 2U);
        EXPECT_EQ((*saved)[0].error.field, "rpe");
        EXPECT_EQ((*saved)[1].error.field, "minutes_override");
    }
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance"), 0);
}

TEST_F(TrainingServiceTest, PlayerGivenTwiceIsReported) {
    logInAs(Role::Coach);
    const std::vector<AttendanceDraft> rows{present(kPlayer, 5), present(kPlayer, 6)};
    const auto saved = m_service->saveAttendance(kSession, rows);
    ASSERT_TRUE(saved.has_value());
    ASSERT_EQ(saved->size(), 1U);
    EXPECT_EQ((*saved)[0].error.field, "player_id");
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance"), 0);
}

TEST_F(TrainingServiceTest, PlayerOfAnotherTeamIsReported) {
    logInAs(Role::Coach);
    ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, primary_position) "
                    "VALUES (3, 2, 'Piotr', 'Obcy', 'cm')"));
    const std::vector<AttendanceDraft> rows{present(kPlayer, 5), present(PlayerId{3}, 5)};
    const auto saved = m_service->saveAttendance(kSession, rows);
    ASSERT_TRUE(saved.has_value());
    ASSERT_EQ(saved->size(), 1U);
    EXPECT_EQ((*saved)[0].player, PlayerId{3});
    EXPECT_EQ((*saved)[0].error.field, "player_id");
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance"), 0);
}

TEST_F(TrainingServiceTest, FailureInTheMiddleKeepsTheOldRows) {
    logInAs(Role::Coach);
    const std::vector<AttendanceDraft> before{present(kPlayer, 5)};
    ASSERT_TRUE(m_service->saveAttendance(kSession, before).has_value());
    // A failure the checks of the service cannot see: the second insert breaks in the database.
    ASSERT_TRUE(sql("CREATE TRIGGER no_nowak BEFORE INSERT ON attendance WHEN NEW.player_id = 2 "
                    "BEGIN SELECT RAISE(ABORT, 'no'); END"));
    const std::vector<AttendanceDraft> rows{present(kPlayer, 9), present(kOtherPlayer, 9)};
    const auto saved = m_service->saveAttendance(kSession, rows);
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(storedRow(kPlayer)->rpe, 5);
    EXPECT_FALSE(storedRow(kOtherPlayer).has_value());
}

TEST_F(TrainingServiceTest, SavingAgainKeepsTheRowsOfArchivedPlayers) {
    logInAs(Role::Coach);
    const std::vector<AttendanceDraft> first{present(kPlayer, 5), present(kOtherPlayer, 7, 60)};
    ASSERT_TRUE(m_service->saveAttendance(kSession, first).has_value());
    ASSERT_TRUE(m_players->archive(kOtherPlayer, kDayAfter).has_value());

    // What the grid sends: the archived player is not on it.
    const std::vector<AttendanceDraft> second{present(kPlayer, 8)};
    const auto saved = m_service->saveAttendance(kSession, second);
    ASSERT_TRUE(saved.has_value()) << saved.error().detail;
    EXPECT_TRUE(saved->empty());

    EXPECT_EQ(storedRow(kPlayer)->rpe, 8);
    ASSERT_TRUE(storedRow(kOtherPlayer).has_value());
    EXPECT_EQ(storedRow(kOtherPlayer)->rpe, 7);
    EXPECT_EQ(storedRow(kOtherPlayer)->minutesOverride, 60);
}

TEST_F(TrainingServiceTest, RowOfAnArchivedPlayerMayStillBeGiven) {
    logInAs(Role::Coach);
    ASSERT_TRUE(m_players->archive(kOtherPlayer, kDayAfter).has_value());
    const std::vector<AttendanceDraft> rows{present(kOtherPlayer, 4)};
    const auto saved = m_service->saveAttendance(kSession, rows);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(saved->empty());
    EXPECT_EQ(storedRow(kOtherPlayer)->rpe, 4);
}

TEST_F(TrainingServiceTest, SaveThenReadGivesTheSameValues) {
    logInAs(Role::Coach);
    const std::vector<AttendanceDraft> rows{present(kPlayer, 10, 75),
                                            withStatus(kOtherPlayer, AttendanceStatus::Excused)};
    ASSERT_TRUE(m_service->saveAttendance(kSession, rows).has_value());
    EXPECT_EQ(sheetRow(kPlayer).draft, rows[0]);
    EXPECT_EQ(sheetRow(kOtherPlayer).draft, rows[1]);
}

} // namespace
