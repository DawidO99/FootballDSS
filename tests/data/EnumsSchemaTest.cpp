// Every text the core enumerations produce must pass the CHECK of its column: the two lists live in
// different languages and nothing else keeps them together.

#include "RepositoryFixture.hpp"
#include "core/domain/Enums.hpp"
#include "data/db/Transaction.hpp"

#include <gtest/gtest.h>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;

namespace {

class EnumsSchemaTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    // Each value is tried inside a transaction that is rolled back, so rows cannot collide.
    template <class E> void expectAccepted(const char* statement) {
        SCOPED_TRACE(statement);
        for (const E value : allValues<E>()) {
            auto transaction = Transaction::begin(*m_db);
            ASSERT_TRUE(transaction.has_value());
            const QString text = QString::fromUtf8(toDbText(value));
            EXPECT_TRUE(sql(statement, {text})) << toDbText(value);
        }
    }
};

} // namespace

TEST_F(EnumsSchemaTest, EveryTextPassesTheCheckOfItsColumn) {
    expectAccepted<TeamCategory>("INSERT INTO team (club_id, name, season, category) "
                                 "VALUES (1, 'X', '2026/27', ?)");
    expectAccepted<Role>("INSERT INTO staff_user (name, pin_hash, salt, iterations, role) "
                         "VALUES ('X', x'00', x'00', 1, ?)");
    expectAccepted<Position>("INSERT INTO player (team_id, first_name, last_name, "
                             "primary_position) VALUES (1, 'A', 'B', ?)");
    expectAccepted<Position>("INSERT INTO appearance (match_id, player_id, position) "
                             "VALUES (1, 1, ?)");
    expectAccepted<PreferredFoot>("INSERT INTO player (team_id, first_name, last_name, "
                                  "primary_position, foot) VALUES (1, 'A', 'B', 'st', ?)");
    expectAccepted<AvailabilityType>("INSERT INTO availability (player_id, from_date, type) "
                                     "VALUES (1, '2026-11-01', ?)");
    expectAccepted<MatchStatus>("INSERT INTO match (team_id, opponent_id, date, venue, status) "
                                "VALUES (1, 1, '2026-11-01', 'home', ?)");
    expectAccepted<Venue>("INSERT INTO match (team_id, opponent_id, date, venue) "
                          "VALUES (1, 1, '2026-11-01', ?)");
    expectAccepted<SessionType>("INSERT INTO training_session (team_id, date, duration_min, type) "
                                "VALUES (1, '2026-11-01', 90, ?)");
    expectAccepted<AttendanceStatus>("INSERT INTO attendance (session_id, player_id, status) "
                                     "VALUES (1, 1, ?)");
    expectAccepted<MatchEventType>("INSERT INTO match_event (match_id, minute, player_id, type) "
                                   "VALUES (1, 10, 1, ?)");
    expectAccepted<MetricValueType>("INSERT INTO metric_definition (key, name_pl, name_en, "
                                    "aggregation, scope, value_type) "
                                    "VALUES ('k', 'x', 'x', 'sum', 'match', ?)");
    expectAccepted<MetricAggregation>("INSERT INTO metric_definition (key, name_pl, name_en, "
                                      "value_type, scope, aggregation) "
                                      "VALUES ('k', 'x', 'x', 'integer', 'match', ?)");
    expectAccepted<MetricScope>("INSERT INTO metric_definition (key, name_pl, name_en, value_type, "
                                "aggregation, scope) VALUES ('k', 'x', 'x', 'integer', 'sum', ?)");
}
