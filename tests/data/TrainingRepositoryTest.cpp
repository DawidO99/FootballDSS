#include "data/repositories/TrainingRepository.hpp"

#include "RepositoryFixture.hpp"
#include "data/db/Transaction.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

constexpr Date kDate = 2026y / November / 10d;
constexpr TrainingSessionId kNoSession{999};

// kSession of the fixture is on this date.
constexpr Date kSeededDate = 2026y / November / 2d;

class TrainingRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    [[nodiscard]] TrainingRepository repository() { return TrainingRepository(*m_db); }

    static TrainingSessionDraft draft(Date date = kDate) {
        TrainingSessionDraft result;
        result.date = date;
        return result;
    }

    TrainingSessionId create(Date date = kDate, TeamId team = kTeam) {
        const auto id = repository().create(team, draft(date));
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(TrainingSessionId{});
    }

    static AttendanceDraft present(PlayerId player, std::optional<int> rpe = std::nullopt,
                                   std::optional<int> minutes = std::nullopt) {
        AttendanceDraft result;
        result.playerId = player;
        result.status = AttendanceStatus::Present;
        result.rpe = rpe;
        result.minutesOverride = minutes;
        return result;
    }

    static AttendanceDraft absent(PlayerId player) {
        AttendanceDraft result;
        result.playerId = player;
        result.status = AttendanceStatus::Absent;
        return result;
    }

    void expectCode(const Status& status, ErrorCode code) {
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, code) << status.error().detail;
    }

    [[nodiscard]] std::vector<TrainingSessionId> idsIn(DateRange range) {
        const auto sessions = repository().list(kTeam, range);
        EXPECT_TRUE(sessions.has_value());
        std::vector<TrainingSessionId> ids;
        for (const TrainingSession& session : sessions.value_or(std::vector<TrainingSession>{})) {
            ids.push_back(session.id);
        }
        return ids;
    }
};

} // namespace

// --- Sessions ---

TEST_F(TrainingRepositoryTest, CreatedSessionKeepsEveryField) {
    TrainingSessionDraft full = draft();
    full.startTime = minutes{18 * 60};
    full.durationMin = 75;
    full.type = SessionType::MatchPrep;
    full.note = "  stałe fragmenty ";
    const auto id = repository().create(kTeam, full);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto session = repository().get(*id);
    ASSERT_TRUE(session.has_value()) << session.error().detail;
    EXPECT_EQ(session->id, *id);
    EXPECT_EQ(session->teamId, kTeam);
    EXPECT_EQ(session->date, kDate);
    EXPECT_EQ(session->startTime, minutes{18 * 60});
    EXPECT_EQ(session->durationMin, 75);
    EXPECT_EQ(session->type, SessionType::MatchPrep);
    EXPECT_EQ(session->note, "stałe fragmenty");
}

TEST_F(TrainingRepositoryTest, OptionalFieldsStayEmpty) {
    const auto session = repository().get(create());
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->startTime, std::nullopt);
    EXPECT_EQ(session->note, std::nullopt);
}

TEST_F(TrainingRepositoryTest, GetOfAMissingSessionIsNotFound) {
    const auto session = repository().get(kNoSession);
    ASSERT_FALSE(session.has_value());
    EXPECT_EQ(session.error().code, ErrorCode::NotFound);
}

TEST_F(TrainingRepositoryTest, SessionOfAMissingTeamIsInvalidInput) {
    const auto id = repository().create(kNoTeam, draft());
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(TrainingRepositoryTest, DurationOutOfBoundsIsInvalidInput) {
    for (const int duration : {14, 241}) {
        TrainingSessionDraft bad = draft();
        bad.durationMin = duration;
        const auto id = repository().create(kTeam, bad);
        ASSERT_FALSE(id.has_value()) << duration;
        EXPECT_EQ(id.error().code, ErrorCode::InvalidInput) << duration;
    }
}

TEST_F(TrainingRepositoryTest, ListIsLimitedToTheTeamAndSortedByDate) {
    const TrainingSessionId later = create(2026y / December / 1d);
    const TrainingSessionId earlier = create(2026y / October / 1d);
    create(kDate, kOtherTeam);
    EXPECT_EQ(idsIn({}), (std::vector{earlier, kSession, later}));
}

TEST_F(TrainingRepositoryTest, SessionsOfOneDayAreSortedByStartTime) {
    TrainingSessionDraft evening = draft();
    evening.startTime = minutes{18 * 60};
    TrainingSessionDraft morning = draft();
    morning.startTime = minutes{9 * 60};
    const TrainingSessionId eveningId = repository().create(kTeam, evening).value();
    const TrainingSessionId morningId = repository().create(kTeam, morning).value();
    EXPECT_EQ(idsIn({kDate, kDate}), (std::vector{morningId, eveningId}));
}

TEST_F(TrainingRepositoryTest, ListRangeIncludesBothEndsAndCanBeOpen) {
    const Date from = 2026y / November / 10d;
    const Date to = 2026y / November / 12d;
    create(2026y / November / 9d);
    const TrainingSessionId first = create(from);
    const TrainingSessionId last = create(to);
    const TrainingSessionId after = create(2026y / November / 13d);

    EXPECT_EQ(idsIn({from, to}), (std::vector{first, last}));
    EXPECT_EQ(idsIn({std::nullopt, kSeededDate}), std::vector{kSession});
    EXPECT_EQ(idsIn({2026y / November / 13d, std::nullopt}), std::vector{after});
}

TEST_F(TrainingRepositoryTest, UpdateChangesEveryField) {
    TrainingSessionDraft changed = draft(2026y / December / 24d);
    changed.startTime = minutes{600};
    changed.durationMin = 240;
    changed.type = SessionType::Gym;
    changed.note = "siłownia";
    ASSERT_TRUE(repository().update(kSession, changed).has_value());

    const auto session = repository().get(kSession);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->date, 2026y / December / 24d);
    EXPECT_EQ(session->startTime, minutes{600});
    EXPECT_EQ(session->durationMin, 240);
    EXPECT_EQ(session->type, SessionType::Gym);
    EXPECT_EQ(session->note, "siłownia");
    EXPECT_EQ(session->teamId, kTeam);
}

TEST_F(TrainingRepositoryTest, UpdateOfAMissingSessionIsNotFound) {
    expectCode(repository().update(kNoSession, draft()), ErrorCode::NotFound);
}

TEST_F(TrainingRepositoryTest, DeletingASessionRemovesItsAttendance) {
    const TrainingSessionId other = create();
    const std::vector rows{present(kPlayer, 5)};
    ASSERT_TRUE(repository().saveAttendance(kSession, rows).has_value());
    ASSERT_TRUE(repository().saveAttendance(other, rows).has_value());

    ASSERT_TRUE(repository().remove(kSession).has_value());
    EXPECT_FALSE(repository().get(kSession).has_value());
    EXPECT_EQ(count("SELECT count(*) FROM attendance"), 1);
    EXPECT_EQ(repository().listAttendance(other)->size(), 1U);
}

TEST_F(TrainingRepositoryTest, DeleteOfAMissingSessionIsNotFound) {
    expectCode(repository().remove(kNoSession), ErrorCode::NotFound);
}

// Known limitation in the P06 Decisions.
TEST_F(TrainingRepositoryTest, DeleteOfASessionWithMetricValuesIsAConflict) {
    ASSERT_TRUE(sql("INSERT INTO metric_definition (id, key, name_pl, name_en, value_type, "
                    "aggregation, scope) VALUES (1, 'k', 'x', 'x', 'integer', 'sum', 'training')"));
    ASSERT_TRUE(sql("INSERT INTO metric_value (metric_id, player_id, session_id, date, value) "
                    "VALUES (1, 1, 1, '2026-11-02', 5)"));
    expectCode(repository().remove(kSession), ErrorCode::Conflict);
    EXPECT_TRUE(repository().get(kSession).has_value());
}

// --- Attendance ---

TEST_F(TrainingRepositoryTest, AttendanceKeepsEveryField) {
    const std::vector rows{present(kPlayer, 7, 60), absent(kOtherPlayer)};
    ASSERT_TRUE(repository().saveAttendance(kSession, rows).has_value());

    const auto stored = repository().listAttendance(kSession);
    ASSERT_TRUE(stored.has_value()) << stored.error().detail;
    ASSERT_EQ(stored->size(), 2U);
    EXPECT_EQ(stored->at(0).sessionId, kSession);
    EXPECT_EQ(stored->at(0).playerId, kPlayer);
    EXPECT_EQ(stored->at(0).status, AttendanceStatus::Present);
    EXPECT_EQ(stored->at(0).rpe, 7);
    EXPECT_EQ(stored->at(0).minutesOverride, 60);
    EXPECT_EQ(stored->at(1).playerId, kOtherPlayer);
    EXPECT_EQ(stored->at(1).status, AttendanceStatus::Absent);
    EXPECT_EQ(stored->at(1).rpe, std::nullopt);
    EXPECT_EQ(stored->at(1).minutesOverride, std::nullopt);
}

TEST_F(TrainingRepositoryTest, SaveAttendanceReplacesAllRowsOfTheSession) {
    const std::vector before{present(kPlayer, 5), present(kOtherPlayer, 6)};
    ASSERT_TRUE(repository().saveAttendance(kSession, before).has_value());
    const std::vector after{absent(kPlayer)};
    ASSERT_TRUE(repository().saveAttendance(kSession, after).has_value());

    const auto stored = repository().listAttendance(kSession);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), 1U);
    EXPECT_EQ(stored->at(0).status, AttendanceStatus::Absent);
}

TEST_F(TrainingRepositoryTest, SaveWithAnEmptyListClearsTheRows) {
    const std::vector before{present(kPlayer)};
    ASSERT_TRUE(repository().saveAttendance(kSession, before).has_value());
    ASSERT_TRUE(repository().saveAttendance(kSession, {}).has_value());
    EXPECT_TRUE(repository().listAttendance(kSession)->empty());
}

TEST_F(TrainingRepositoryTest, SaveAttendanceIsAtomic) {
    const std::vector before{present(kPlayer, 5)};
    ASSERT_TRUE(repository().saveAttendance(kSession, before).has_value());

    // The first new row is fine; the second one breaks the CHECK on rpe.
    const std::vector broken{present(kPlayer, 9), present(kOtherPlayer, 11)};
    expectCode(repository().saveAttendance(kSession, broken), ErrorCode::InvalidInput);

    const auto stored = repository().listAttendance(kSession);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), 1U);
    EXPECT_EQ(stored->at(0).rpe, 5);
}

TEST_F(TrainingRepositoryTest, RpeOnAnAbsentPlayerIsInvalidInputAndSavesNothing) {
    AttendanceDraft bad = absent(kOtherPlayer);
    bad.rpe = 5;
    const std::vector rows{present(kPlayer), bad};
    expectCode(repository().saveAttendance(kSession, rows), ErrorCode::InvalidInput);
    EXPECT_TRUE(repository().listAttendance(kSession)->empty());
}

TEST_F(TrainingRepositoryTest, SamePlayerTwiceIsAConflictAndSavesNothing) {
    const std::vector twice{present(kPlayer), absent(kPlayer)};
    const auto status = repository().saveAttendance(kSession, twice);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::Conflict);
    EXPECT_EQ(status.error().field, "player_id");
    EXPECT_TRUE(repository().listAttendance(kSession)->empty());
}

TEST_F(TrainingRepositoryTest, AttendanceOfAMissingPlayerIsInvalidInput) {
    const std::vector rows{present(kNoPlayer)};
    expectCode(repository().saveAttendance(kSession, rows), ErrorCode::InvalidInput);
}

TEST_F(TrainingRepositoryTest, SaveForAMissingSessionIsNotFoundEvenWithoutRows) {
    expectCode(repository().saveAttendance(kNoSession, {}), ErrorCode::NotFound);
    const std::vector rows{present(kPlayer)};
    expectCode(repository().saveAttendance(kNoSession, rows), ErrorCode::NotFound);
}

TEST_F(TrainingRepositoryTest, SaveJoinsATransactionOfTheCaller) {
    {
        auto outer = Transaction::begin(*m_db);
        ASSERT_TRUE(outer.has_value());
        const std::vector rows{present(kPlayer)};
        ASSERT_TRUE(repository().saveAttendance(kSession, rows).has_value());
    }
    EXPECT_TRUE(repository().listAttendance(kSession)->empty());
}

TEST_F(TrainingRepositoryTest, FailedSaveLeavesTheWorkOfTheCaller) {
    auto outer = Transaction::begin(*m_db);
    ASSERT_TRUE(outer.has_value());
    const std::vector good{present(kPlayer, 5)};
    ASSERT_TRUE(repository().saveAttendance(kSession, good).has_value());

    const std::vector broken{present(kPlayer, 11)};
    EXPECT_FALSE(repository().saveAttendance(kSession, broken).has_value());
    ASSERT_TRUE(outer->commit().has_value());

    const auto stored = repository().listAttendance(kSession);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), 1U);
    EXPECT_EQ(stored->at(0).rpe, 5);
}

TEST_F(TrainingRepositoryTest, AttendanceOfAnotherSessionStaysUntouched) {
    const TrainingSessionId other = create();
    const std::vector rows{present(kPlayer)};
    ASSERT_TRUE(repository().saveAttendance(other, rows).has_value());
    ASSERT_TRUE(repository().saveAttendance(kSession, rows).has_value());
    ASSERT_TRUE(repository().saveAttendance(kSession, {}).has_value());
    EXPECT_EQ(repository().listAttendance(other)->size(), 1U);
}

TEST_F(TrainingRepositoryTest, AttendanceByPlayerCarriesTheSessionDateAndDuration) {
    TrainingSessionDraft longSession = draft();
    longSession.durationMin = 120;
    const TrainingSessionId session = repository().create(kTeam, longSession).value();
    const std::vector rows{present(kPlayer, 6, 100), absent(kOtherPlayer)};
    ASSERT_TRUE(repository().saveAttendance(session, rows).has_value());

    const auto stored = repository().listAttendanceByPlayer(kPlayer, {});
    ASSERT_TRUE(stored.has_value()) << stored.error().detail;
    ASSERT_EQ(stored->size(), 1U);
    EXPECT_EQ(stored->at(0).attendance.sessionId, session);
    EXPECT_EQ(stored->at(0).attendance.playerId, kPlayer);
    EXPECT_EQ(stored->at(0).attendance.rpe, 6);
    EXPECT_EQ(stored->at(0).attendance.minutesOverride, 100);
    EXPECT_EQ(stored->at(0).date, kDate);
    EXPECT_EQ(stored->at(0).durationMin, 120);
}

TEST_F(TrainingRepositoryTest, AttendanceByPlayerIsLimitedToTheRangeAndSortedByDate) {
    const Date from = 2026y / November / 10d;
    const Date to = 2026y / November / 12d;
    const TrainingSessionId last = create(to);
    const TrainingSessionId first = create(from);
    const TrainingSessionId before = create(2026y / November / 9d);
    const TrainingSessionId after = create(2026y / November / 13d);
    const std::vector rows{present(kPlayer)};
    for (const TrainingSessionId session : {last, first, before, after}) {
        ASSERT_TRUE(repository().saveAttendance(session, rows).has_value());
    }

    const auto inRange = repository().listAttendanceByPlayer(kPlayer, {from, to});
    ASSERT_TRUE(inRange.has_value());
    ASSERT_EQ(inRange->size(), 2U);
    EXPECT_EQ(inRange->at(0).attendance.sessionId, first);
    EXPECT_EQ(inRange->at(1).attendance.sessionId, last);

    EXPECT_EQ(repository().listAttendanceByPlayer(kPlayer, {std::nullopt, to})->size(), 3U);
    EXPECT_EQ(repository().listAttendanceByPlayer(kPlayer, {from, std::nullopt})->size(), 3U);
    EXPECT_EQ(repository().listAttendanceByPlayer(kPlayer, {})->size(), 4U);
    EXPECT_TRUE(repository().listAttendanceByPlayer(kOtherPlayer, {})->empty());
}

// Packet P18 decision 11: saving the sheet keeps the import_id of an imported row, and the undo of
// the import finds its rows and sessions.
TEST_F(TrainingRepositoryTest, ImportedRowsKeepTheirImportAcrossTheSheet) {
    ASSERT_TRUE(sql("INSERT INTO import_log (id, timestamp, file, hash, type) "
                    "VALUES (7, '2026-11-01T10:00:00Z', 'f.csv', 'h', 'training')"));
    const auto session = repository().create(kTeam, draft(), ImportId{7});
    ASSERT_TRUE(session.has_value());
    ASSERT_TRUE(repository().insertAttendance(*session, present(kPlayer, 6), ImportId{7}));
    // Corrected on the sheet, with a row added by hand.
    const std::vector rows = {present(kPlayer, 8), present(kOtherPlayer, 5)};
    ASSERT_TRUE(repository().saveAttendance(*session, rows).has_value());
    EXPECT_EQ(count("SELECT count(*) FROM attendance WHERE import_id = 7"), 1);
    EXPECT_EQ(repository().findAttendance(*session, kPlayer)->value().rpe, 8);
    EXPECT_EQ(*repository().countAttendanceAddedLater(ImportId{7}), 1);
    ASSERT_TRUE(repository().updateAttendance(*session, present(kPlayer, 3)).has_value());
    EXPECT_EQ(count("SELECT count(*) FROM attendance WHERE import_id = 7 AND rpe = 3"), 1);
    EXPECT_EQ(repository().listByImport(ImportId{7})->size(), 1U);
    EXPECT_EQ(*repository().removeAttendanceByImport(ImportId{7}), 1);
    EXPECT_EQ(*repository().removeByImport(ImportId{7}), 1);
    EXPECT_EQ(count("SELECT count(*) FROM attendance"), 0);
}
