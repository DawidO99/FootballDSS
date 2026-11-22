// Upgrade of a real database from every older schema version to the latest (P19 decision 10):
// nothing is lost, and the rebuilt player table keeps every row that points at it.

#include "data/db/Migrations.hpp"

#include "TestDatabase.hpp"
#include "data/db/DatabaseManager.hpp"

#include <QSqlQuery>

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

using namespace fdss::data;
using namespace fdss::tests;

namespace {

constexpr int kVersionWithMustChangePin = 5;

// Rows of every table that version 1 has, with players referenced from every kind of child.
const std::vector<const char*> kVersion1Rows = {
    "INSERT INTO import_log (id, timestamp, file, hash, type) "
    "VALUES (1, '2026-09-01T10:00:00Z', 'kadra.csv', 'abc', 'squad')",
    "INSERT INTO club (id, name) VALUES (1, 'KS Test')",
    "INSERT INTO team (id, club_id, name, category, season) "
    "VALUES (1, 1, 'Seniorzy', 'senior', '2026/27')",
    "INSERT INTO staff_user (id, name, role, pin_hash, salt, iterations) "
    "VALUES (1, 'Jan Trener', 'coach', x'01', x'02', 4)",
    "INSERT INTO staff_user (id, name, role, pin_hash, salt, iterations, failed_attempts) "
    "VALUES (2, 'Ewa Fizjo', 'physio', x'03', x'04', 4, 2)",
    "INSERT INTO player (id, team_id, first_name, last_name, shirt_number, birth_year, "
    "primary_position, secondary_positions, foot, notes) "
    "VALUES (1, 1, 'Adam', 'Bramka', 1, 1998, 'gk', '', 'right', 'Kapitan')",
    "INSERT INTO player (id, team_id, first_name, last_name, shirt_number, primary_position, "
    "secondary_positions, import_id) VALUES (2, 1, 'Bartek', 'Obrona', 4, 'cb', 'rb,lb', 1)",
    "INSERT INTO player (id, team_id, first_name, last_name, shirt_number, primary_position, "
    "archived_at) VALUES (3, 1, 'Czarek', 'Stary', 4, 'st', '2026-08-01')",
    "INSERT INTO availability (player_id, type, from_date, expected_to, description) "
    "VALUES (2, 'injury', '2026-09-10', '2026-09-30', 'Kolano')",
    "INSERT INTO opponent (id, name, name_key) VALUES (1, 'Lech', 'lech')",
    "INSERT INTO match (id, team_id, opponent_id, date, venue, goals_for, goals_against, status, "
    "formation) VALUES (1, 1, 1, '2026-09-20', 'home', 2, 1, 'finished', '4-4-2')",
    "INSERT INTO appearance (match_id, player_id, started, minute_on, minute_off, position, "
    "rating) "
    "VALUES (1, 1, 1, 0, 90, 'gk', 7)",
    "INSERT INTO appearance (match_id, player_id, started, minute_on, minute_off, position) "
    "VALUES (1, 2, 1, 0, 60, 'cb')",
    "INSERT INTO match_event (match_id, minute, type, player_id, related_player_id) "
    "VALUES (1, 60, 'sub', 3, 2)",
    "INSERT INTO match_event (match_id, minute, type) VALUES (1, 70, 'opponent_goal')",
    "INSERT INTO training_session (id, team_id, date, duration_min, type) "
    "VALUES (1, 1, '2026-09-18', 90, 'training')",
    "INSERT INTO attendance (session_id, player_id, status, rpe) VALUES (1, 1, 'present', 6)",
    "INSERT INTO attendance (session_id, player_id, status) VALUES (1, 2, 'injured')",
    "INSERT INTO wellness (player_id, date, sleep, fatigue, soreness, stress, mood) "
    "VALUES (1, '2026-09-18', 4, 3, 2, 2, 4)",
    "INSERT INTO metric_definition (id, key, name_pl, name_en, value_type, aggregation, scope) "
    "VALUES (1, 'sprint', 'Sprinty', 'Sprints', 'integer', 'sum', 'training')",
    "INSERT INTO metric_value (metric_id, player_id, session_id, date, value) "
    "VALUES (1, 2, 1, '2026-09-18', 12)",
    "INSERT INTO player_attribute (player_id, attribute, rating, date) "
    "VALUES (1, 'reflexes', 8, '2026-09-01')",
    "INSERT INTO lineup (match_id, formation, tactic_json) VALUES (1, '4-4-2', "
    "'{\"version\": 1, \"formation\": \"4-4-2\", \"slots\": [{\"position\": \"GK\", "
    "\"x\": 0.5, \"y\": 0.95, \"playerId\": 1}], \"bench\": [3]}')",
    "INSERT INTO event_data_file (match_id, provider, path, hash) "
    "VALUES (1, 'statsbomb', 'events.json', 'def')",
};

// Rows of the tables that later versions add.
const std::map<int, std::vector<const char*>> kLaterRows = {
    {2,
     {"INSERT INTO match_live (match_id, periods, period_length, next_seq, finished_seq) "
      "VALUES (1, 2, 45, 5, 4)",
      "INSERT INTO match_clock_step (match_id, period, kind, at, seq) "
      "VALUES (1, 1, 'start', '2026-09-20T15:00:00Z', 1)",
      "INSERT INTO match_note (match_id, minute, player_id, text, seq) "
      "VALUES (1, 30, 2, 'Dobra gra', 2)"}},
    {3,
     {"INSERT INTO match_tactic_change (match_id, minute, formation, tactic_json, seq) "
      "VALUES (1, 55, '4-3-3', '{\"version\": 1, \"formation\": \"4-3-3\", \"slots\": []}', 3)"}},
    {4, {"UPDATE match SET report_note = 'Dobry mecz' WHERE id = 1"}},
};

std::vector<Migration> firstMigrations(const std::vector<Migration>& all, int version) {
    return {all.begin(), all.begin() + version};
}

// Every table but schema_version, with its number of rows.
std::map<std::string, int> rowCounts(const QSqlDatabase& db) {
    std::map<std::string, int> counts;
    QSqlQuery tables(db);
    EXPECT_TRUE(tables.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name <> 'schema_version'")));
    while (tables.next()) {
        const QString table = tables.value(0).toString();
        // Test code: the name comes from sqlite_master, not from a user.
        counts[table.toStdString()] =
            scalar(db, QStringLiteral("SELECT count(*) FROM \"%1\"").arg(table)).toInt();
    }
    return counts;
}

class MigrationUpgradeTest : public TempDatabaseTest, public ::testing::WithParamInterface<int> {};

} // namespace

TEST_P(MigrationUpgradeTest, KeepsEveryRowAndEveryPosition) {
    const int fromVersion = GetParam();
    const auto all = embeddedMigrations();
    ASSERT_TRUE(all.has_value()) << all.error().detail;
    ASSERT_GE(static_cast<int>(all->size()), kVersionWithMustChangePin);

    std::map<std::string, int> before;
    {
        const auto old =
            DatabaseManager::open(databasePath(), options(firstMigrations(*all, fromVersion)));
        ASSERT_TRUE(old.has_value()) << old.error().detail;
        ASSERT_EQ((*old)->schemaVersion(), fromVersion);
        const QSqlDatabase db = (*old)->database();
        for (const char* row : kVersion1Rows) {
            ASSERT_TRUE(run(db, QString::fromUtf8(row))) << row;
        }
        for (const auto& [version, rows] : kLaterRows) {
            for (const char* row : rows) {
                if (version <= fromVersion) {
                    ASSERT_TRUE(run(db, QString::fromUtf8(row))) << row;
                }
            }
        }
        before = rowCounts(db);
    }

    const auto upgraded = DatabaseManager::open(databasePath(), options(*all));
    ASSERT_TRUE(upgraded.has_value()) << upgraded.error().detail;
    EXPECT_EQ((*upgraded)->schemaVersion(), static_cast<int>(all->size()));
    const QSqlDatabase db = (*upgraded)->database();

    const std::map<std::string, int> after = rowCounts(db);
    for (const auto& [table, count] : before) {
        ASSERT_TRUE(after.contains(table)) << table;
        EXPECT_EQ(after.at(table), count) << table;
    }
    EXPECT_FALSE(after.contains("player_new"));

    EXPECT_EQ(scalar(db, QStringLiteral("SELECT group_concat(id || ':' || primary_position || ':' "
                                        "|| secondary_positions, ';') FROM (SELECT * FROM player "
                                        "ORDER BY id)"))
                  .toString(),
              QStringLiteral("1:gk:;2:cb:rb,lb;3:st:"));
    EXPECT_EQ(scalar(db, QStringLiteral("SELECT notes FROM player WHERE id = 1")).toString(),
              QStringLiteral("Kapitan"));
    EXPECT_EQ(scalar(db, QStringLiteral("SELECT import_id FROM player WHERE id = 2")).toInt(), 1);
    EXPECT_EQ(scalar(db, QStringLiteral("SELECT archived_at FROM player WHERE id = 3")).toString(),
              QStringLiteral("2026-08-01"));
    EXPECT_EQ(
        scalar(db, QStringLiteral("SELECT count(*) FROM staff_user WHERE must_change_pin = 0"))
            .toInt(),
        2);
    EXPECT_EQ(
        scalar(db, QStringLiteral("SELECT failed_attempts FROM staff_user WHERE id = 2")).toInt(),
        2);

    QSqlQuery check(db);
    ASSERT_TRUE(check.exec(QStringLiteral("PRAGMA foreign_key_check")));
    EXPECT_FALSE(check.next());
    EXPECT_EQ(scalar(db, QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);

    // The rebuilt table takes "not given", still refuses an unknown code and keeps its indexes.
    EXPECT_TRUE(run(db, QStringLiteral("INSERT INTO player (team_id, first_name, last_name) "
                                       "VALUES (1, 'Darek', 'Bez Pozycji')")));
    EXPECT_FALSE(run(db, QStringLiteral("INSERT INTO player (team_id, first_name, last_name, "
                                        "primary_position) VALUES (1, 'Ela', 'Zła', 'xx')")));
    EXPECT_FALSE(run(db, QStringLiteral("INSERT INTO player (team_id, first_name, last_name, "
                                        "shirt_number) VALUES (1, 'Filip', 'Dubel', 1)")));
    EXPECT_FALSE(run(db, QStringLiteral("DELETE FROM player WHERE id = 1")));
    EXPECT_EQ(scalar(db, QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type = 'index' "
                                        "AND tbl_name = 'player' AND (name LIKE 'ix_player_%' OR "
                                        "name = 'ux_player_team_shirt')"))
                  .toInt(),
              3);
}

INSTANTIATE_TEST_SUITE_P(FromEveryOlderVersion, MigrationUpgradeTest, ::testing::Values(1, 2, 3, 4),
                         [](const ::testing::TestParamInfo<int>& info) {
                             return "FromVersion" + std::to_string(info.param);
                         });
