// Checks that 0001_initial.sql matches SPEC section 6: tables, constraints, ON DELETE rules,
// indexes.

#include "TestDatabase.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/db/Transaction.hpp"

#include <QSqlQuery>
#include <QVersionNumber>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

using namespace fdss::data;
using namespace fdss::tests;

namespace {

// One column of one table: the statement has a single ? for the value under test.
struct ColumnRule {
    const char* column;
    QString insert;
    QVariantList accepted;
    QVariantList rejected;
};

const QVariant kNull;

QVariantList texts(std::initializer_list<const char*> values) {
    QVariantList result;
    for (const char* value : values) {
        result.append(QString::fromUtf8(value));
    }
    return result;
}

QString insertPlayerWith(const char* column) {
    return QStringLiteral(
               "INSERT INTO player (team_id, first_name, last_name, primary_position, %1) "
               "VALUES (1, 'Adam', 'Nowak', 'st', ?)")
        .arg(QString::fromLatin1(column));
}

QString insertWellnessWith(const char* column) {
    QString columns;
    QString values;
    for (const char* name : {"sleep", "fatigue", "soreness", "stress", "mood"}) {
        const bool isTested = qstrcmp(name, column) == 0;
        columns += QStringLiteral(", %1").arg(QString::fromLatin1(name));
        values += isTested ? QStringLiteral(", ?") : QStringLiteral(", 3");
    }
    return QStringLiteral("INSERT INTO wellness (player_id, date%1) VALUES (1, '2026-11-01'%2)")
        .arg(columns, values);
}

class SchemaTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto opened = DatabaseManager::openInMemory();
        ASSERT_TRUE(opened.has_value()) << opened.error().detail;
        m_manager = std::move(*opened);

        ASSERT_TRUE(sql("INSERT INTO club (id, name) VALUES (1, 'KS Test')"));
        ASSERT_TRUE(sql("INSERT INTO team (id, club_id, name, category, season) "
                        "VALUES (1, 1, 'Seniorzy', 'senior', '2026/27')"));
        ASSERT_TRUE(sql("INSERT INTO team (id, club_id, name, category, season) "
                        "VALUES (2, 1, 'Juniorzy', 'u19', '2026/27')"));
        ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, shirt_number, "
                        "primary_position) VALUES (1, 1, 'Jan', 'Kowalski', 7, 'cm')"));
        ASSERT_TRUE(sql("INSERT INTO opponent (id, name, name_key) VALUES (1, 'Rywal', 'rywal')"));
        ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue) "
                        "VALUES (1, 1, 1, '2026-11-01', 'home')"));
        ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                        "VALUES (1, 1, '2026-11-02', 90, 'training')"));
        ASSERT_TRUE(
            sql("INSERT INTO metric_definition (id, key, name_pl, name_en, value_type, "
                "aggregation, scope) VALUES (1, 'sprints', 'Sprinty', 'Sprints', 'integer', "
                "'sum', 'match')"));
        ASSERT_TRUE(sql("INSERT INTO import_log (id, timestamp, file, hash, type) "
                        "VALUES (1, '2026-11-01T10:00:00Z', 'a.csv', 'abc', 'squad')"));
    }

    [[nodiscard]] ::testing::AssertionResult sql(const char* statement,
                                                 const QVariantList& values = {}) const {
        return run(m_manager->database(), QString::fromUtf8(statement), values);
    }

    [[nodiscard]] int count(const char* statement) const {
        return scalar(m_manager->database(), QString::fromUtf8(statement)).toInt();
    }

    // Tries each value inside a transaction that is rolled back, so values cannot collide.
    void checkRule(const ColumnRule& rule) {
        SCOPED_TRACE(rule.column);
        const auto attempt = [&](const QVariant& value) {
            auto transaction = Transaction::begin(*m_manager);
            EXPECT_TRUE(transaction.has_value());
            return run(m_manager->database(), rule.insert, {value});
        };
        for (const QVariant& value : rule.accepted) {
            EXPECT_TRUE(attempt(value)) << "should accept: " << value.toString().toStdString();
        }
        for (const QVariant& value : rule.rejected) {
            EXPECT_FALSE(attempt(value)) << "should reject: " << value.toString().toStdString();
        }
    }

    // True when the column is the first column of a non-partial index, so lookups can use it.
    [[nodiscard]] bool leadsAnIndex(const QString& table, const QString& column) const {
        return scalar(m_manager->database(),
                      QStringLiteral("SELECT count(*) FROM pragma_index_list(?) AS list, "
                                     "pragma_index_info(list.name) AS info "
                                     "WHERE info.seqno = 0 AND info.name = ? AND list.partial = 0"),
                      {table, column})
                   .toInt() > 0;
    }

    [[nodiscard]] bool hasColumn(const QString& table, const QString& column) const {
        return scalar(m_manager->database(),
                      QStringLiteral("SELECT count(*) FROM pragma_table_info(?) WHERE name = ?"),
                      {table, column})
                   .toInt() == 1;
    }

    std::unique_ptr<DatabaseManager> m_manager;
};

} // namespace

// --- Tables and columns ---

TEST_F(SchemaTest, HasExactlyTheTablesOfTheSpec) {
    QStringList tables;
    QSqlQuery query(m_manager->database());
    ASSERT_TRUE(query.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' "
                                             "AND name NOT LIKE 'sqlite_%' ORDER BY name")));
    ASSERT_TRUE(query.exec());
    while (query.next()) {
        tables.append(query.value(0).toString());
    }
    const QStringList expected{QStringLiteral("appearance"),
                               QStringLiteral("attendance"),
                               QStringLiteral("availability"),
                               QStringLiteral("club"),
                               QStringLiteral("event_data_file"),
                               QStringLiteral("import_log"),
                               QStringLiteral("lineup"),
                               QStringLiteral("match"),
                               QStringLiteral("match_clock_step"),
                               QStringLiteral("match_event"),
                               QStringLiteral("match_live"),
                               QStringLiteral("metric_definition"),
                               QStringLiteral("metric_value"),
                               QStringLiteral("opponent"),
                               QStringLiteral("player"),
                               QStringLiteral("player_attribute"),
                               QStringLiteral("schema_version"),
                               QStringLiteral("staff_user"),
                               QStringLiteral("team"),
                               QStringLiteral("training_session"),
                               QStringLiteral("wellness")};
    EXPECT_EQ(tables.join(u','), expected.join(u','));
}

TEST_F(SchemaTest, ImportIdIsOnEveryImportableTableAndIndexed) {
    for (const char* table :
         {"player", "match", "appearance", "training_session", "attendance", "metric_value"}) {
        SCOPED_TRACE(table);
        const QString name = QString::fromLatin1(table);
        EXPECT_TRUE(hasColumn(name, QStringLiteral("import_id")));
        EXPECT_TRUE(leadsAnIndex(name, QStringLiteral("import_id")));
    }
}

TEST_F(SchemaTest, EveryForeignKeyHasAnIndex) {
    QSqlQuery keys(m_manager->database());
    ASSERT_TRUE(keys.prepare(
        QStringLiteral("SELECT master.name, fk.\"from\" FROM sqlite_master AS master, "
                       "pragma_foreign_key_list(master.name) AS fk WHERE master.type = 'table'")));
    ASSERT_TRUE(keys.exec());
    int checked = 0;
    while (keys.next()) {
        const QString table = keys.value(0).toString();
        const QString column = keys.value(1).toString();
        EXPECT_TRUE(leadsAnIndex(table, column))
            << table.toStdString() << "." << column.toStdString();
        ++checked;
    }
    EXPECT_GT(checked, 20);
}

TEST_F(SchemaTest, DateColumnsUsedForRangeQueriesAreIndexed) {
    const std::pair<const char*, const char*> columns[] = {
        {"availability", "from_date"}, {"match", "date"},        {"training_session", "date"},
        {"wellness", "date"},          {"metric_value", "date"}, {"player_attribute", "date"}};
    for (const auto& [table, column] : columns) {
        EXPECT_TRUE(leadsAnIndex(QString::fromLatin1(table), QString::fromLatin1(column)))
            << table << "." << column;
    }
}

TEST_F(SchemaTest, ForeignKeyRulesAreConsistent) {
    EXPECT_EQ(count("SELECT count(*) FROM pragma_foreign_key_check"), 0);
    EXPECT_EQ(scalar(m_manager->database(), QStringLiteral("PRAGMA integrity_check")).toString(),
              QStringLiteral("ok"));
}

// Only the pure child rows named by the spec cascade; everything else is an explicit RESTRICT.
TEST_F(SchemaTest, CascadeOnlyWhereTheSpecSaysSo) {
    QSqlQuery keys(m_manager->database());
    ASSERT_TRUE(keys.prepare(QStringLiteral(
        "SELECT master.name, fk.\"from\", fk.on_delete FROM sqlite_master AS master, "
        "pragma_foreign_key_list(master.name) AS fk WHERE master.type = 'table'")));
    ASSERT_TRUE(keys.exec());
    while (keys.next()) {
        const QString key = keys.value(0).toString() + u'.' + keys.value(1).toString();
        const QString onDelete = keys.value(2).toString();
        const bool shouldCascade = key == QStringLiteral("attendance.session_id") ||
                                   key == QStringLiteral("appearance.match_id") ||
                                   key == QStringLiteral("match_event.match_id") ||
                                   key == QStringLiteral("match_live.match_id") ||
                                   key == QStringLiteral("match_clock_step.match_id");
        EXPECT_EQ(onDelete.toStdString(), shouldCascade ? "CASCADE" : "RESTRICT")
            << key.toStdString();
    }
}

// --- Foreign keys in action ---

TEST_F(SchemaTest, ChildWithMissingParentIsRejected) {
    EXPECT_FALSE(sql("INSERT INTO player (team_id, first_name, last_name, primary_position) "
                     "VALUES (999, 'Adam', 'Nowak', 'st')"));
    EXPECT_FALSE(sql("INSERT INTO attendance (session_id, player_id, status) "
                     "VALUES (999, 1, 'absent')"));
    EXPECT_FALSE(sql("INSERT INTO player (team_id, first_name, last_name, primary_position, "
                     "import_id) VALUES (1, 'Adam', 'Nowak', 'st', 999)"));
}

TEST_F(SchemaTest, ParentsInUseCannotBeDeleted) {
    EXPECT_FALSE(sql("DELETE FROM club WHERE id = 1"));
    EXPECT_FALSE(sql("DELETE FROM team WHERE id = 1"));
    EXPECT_FALSE(sql("DELETE FROM opponent WHERE id = 1"));

    ASSERT_TRUE(sql("INSERT INTO attendance (session_id, player_id, status) "
                    "VALUES (1, 1, 'absent')"));
    EXPECT_FALSE(sql("DELETE FROM player WHERE id = 1"));
}

TEST_F(SchemaTest, ImportLogRowInUseCannotBeDeleted) {
    ASSERT_TRUE(sql("UPDATE player SET import_id = 1 WHERE id = 1"));
    EXPECT_FALSE(sql("DELETE FROM import_log WHERE id = 1"));
    ASSERT_TRUE(sql("UPDATE player SET import_id = NULL WHERE id = 1"));
    EXPECT_TRUE(sql("DELETE FROM import_log WHERE id = 1"));
}

TEST_F(SchemaTest, DeletingASessionRemovesItsAttendance) {
    ASSERT_TRUE(sql("INSERT INTO attendance (session_id, player_id, status) "
                    "VALUES (1, 1, 'absent')"));
    ASSERT_TRUE(sql("DELETE FROM training_session WHERE id = 1"));
    EXPECT_EQ(count("SELECT count(*) FROM attendance"), 0);
}

TEST_F(SchemaTest, DeletingAMatchRemovesAppearancesAndEvents) {
    ASSERT_TRUE(sql("INSERT INTO appearance (match_id, player_id) VALUES (1, 1)"));
    ASSERT_TRUE(sql("INSERT INTO match_event (match_id, minute, type, player_id) "
                    "VALUES (1, 10, 'goal', 1)"));
    ASSERT_TRUE(sql("DELETE FROM match WHERE id = 1"));
    EXPECT_EQ(count("SELECT count(*) FROM appearance"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM match_event"), 0);
}

TEST_F(SchemaTest, SessionOrMatchWithMetricValuesCannotBeDeleted) {
    ASSERT_TRUE(sql("INSERT INTO metric_value (metric_id, player_id, session_id, date, value) "
                    "VALUES (1, 1, 1, '2026-11-02', 5)"));
    ASSERT_TRUE(sql("INSERT INTO metric_value (metric_id, player_id, match_id, date, value) "
                    "VALUES (1, 1, 1, '2026-11-01', 5)"));
    EXPECT_FALSE(sql("DELETE FROM training_session WHERE id = 1"));
    EXPECT_FALSE(sql("DELETE FROM match WHERE id = 1"));
}

// --- Shirt numbers ---

TEST_F(SchemaTest, ShirtNumberIsUniqueAmongActivePlayersOfATeam) {
    EXPECT_FALSE(sql("INSERT INTO player (team_id, first_name, last_name, shirt_number, "
                     "primary_position) VALUES (1, 'Adam', 'Nowak', 7, 'st')"));
}

TEST_F(SchemaTest, SameShirtNumberIsFineInAnotherTeam) {
    EXPECT_TRUE(sql("INSERT INTO player (team_id, first_name, last_name, shirt_number, "
                    "primary_position) VALUES (2, 'Adam', 'Nowak', 7, 'st')"));
}

TEST_F(SchemaTest, ArchivedPlayerFreesTheShirtNumber) {
    ASSERT_TRUE(sql("UPDATE player SET archived_at = '2026-11-01' WHERE id = 1"));
    EXPECT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, shirt_number, "
                    "primary_position) VALUES (2, 1, 'Adam', 'Nowak', 7, 'st')"));
    // Bringing the old player back would duplicate the number.
    EXPECT_FALSE(sql("UPDATE player SET archived_at = NULL WHERE id = 1"));
}

TEST_F(SchemaTest, AnyNumberOfPlayersCanHaveNoShirtNumber) {
    for (int i = 0; i < 2; ++i) {
        EXPECT_TRUE(sql("INSERT INTO player (team_id, first_name, last_name, primary_position) "
                        "VALUES (1, 'Adam', 'Nowak', 'st')"));
    }
}

// --- Rules across columns ---

TEST_F(SchemaTest, OpponentNameKeyIsUnique) {
    EXPECT_FALSE(sql("INSERT INTO opponent (name, name_key) VALUES ('RYWAL', 'rywal')"));
    EXPECT_TRUE(sql("INSERT INTO opponent (name, name_key) VALUES ('Śląsk', 'śląsk')"));
}

TEST_F(SchemaTest, RpeAndMinutesOnlyWhenPresent) {
    EXPECT_TRUE(sql("INSERT INTO attendance (session_id, player_id, status, rpe, minutes_override) "
                    "VALUES (1, 1, 'present', 7, 60)"));
    ASSERT_TRUE(sql("DELETE FROM attendance"));
    for (const char* status : {"absent", "excused", "injured"}) {
        SCOPED_TRACE(status);
        EXPECT_FALSE(sql("INSERT INTO attendance (session_id, player_id, status, rpe) "
                         "VALUES (1, 1, ?, 5)",
                         {QString::fromLatin1(status)}));
        EXPECT_FALSE(sql("INSERT INTO attendance (session_id, player_id, status, minutes_override) "
                         "VALUES (1, 1, ?, 30)",
                         {QString::fromLatin1(status)}));
    }
}

TEST_F(SchemaTest, OneAttendanceAndOneAppearancePerPlayer) {
    ASSERT_TRUE(
        sql("INSERT INTO attendance (session_id, player_id, status) VALUES (1, 1, 'absent')"));
    EXPECT_FALSE(
        sql("INSERT INTO attendance (session_id, player_id, status) VALUES (1, 1, 'absent')"));
    ASSERT_TRUE(sql("INSERT INTO appearance (match_id, player_id) VALUES (1, 1)"));
    EXPECT_FALSE(sql("INSERT INTO appearance (match_id, player_id) VALUES (1, 1)"));
}

TEST_F(SchemaTest, OneWellnessEntryPerPlayerAndDay) {
    const char* insert = "INSERT INTO wellness (player_id, date, sleep, fatigue, soreness, stress, "
                         "mood) VALUES (1, ?, 3, 3, 3, 3, 3)";
    ASSERT_TRUE(sql(insert, {QStringLiteral("2026-11-01")}));
    EXPECT_FALSE(sql(insert, {QStringLiteral("2026-11-01")}));
    EXPECT_TRUE(sql(insert, {QStringLiteral("2026-11-02")}));
}

TEST_F(SchemaTest, AvailabilityCannotEndBeforeItStarts) {
    const char* insert = "INSERT INTO availability (player_id, type, from_date, expected_to, "
                         "actual_to) VALUES (1, 'injury', '2026-11-10', ?, ?)";
    EXPECT_TRUE(sql(insert, {QStringLiteral("2026-11-10"), QStringLiteral("2026-11-10")}));
    EXPECT_TRUE(sql(insert, {kNull, kNull}));
    EXPECT_FALSE(sql(insert, {QStringLiteral("2026-11-09"), kNull}));
    EXPECT_FALSE(sql(insert, {kNull, QStringLiteral("2026-11-09")}));
}

TEST_F(SchemaTest, MatchEventNeedsAPlayerExceptOpponentGoal) {
    EXPECT_TRUE(sql("INSERT INTO match_event (match_id, minute, type) "
                    "VALUES (1, 10, 'opponent_goal')"));
    for (const char* type : {"goal", "assist", "yellow", "red", "sub"}) {
        SCOPED_TRACE(type);
        EXPECT_FALSE(sql("INSERT INTO match_event (match_id, minute, type) VALUES (1, 10, ?)",
                         {QString::fromLatin1(type)}));
        EXPECT_TRUE(sql("INSERT INTO match_event (match_id, minute, type, player_id) "
                        "VALUES (1, 10, ?, 1)",
                        {QString::fromLatin1(type)}));
    }
}

TEST_F(SchemaTest, AddedTimeIsEmptyOrAtLeastOne) {
    EXPECT_TRUE(sql("INSERT INTO match_event (match_id, minute, type, added_time) "
                    "VALUES (1, 45, 'opponent_goal', 3)"));
    EXPECT_TRUE(sql("INSERT INTO match_event (match_id, minute, type, added_time) "
                    "VALUES (1, 45, 'opponent_goal', NULL)"));
    EXPECT_FALSE(sql("INSERT INTO match_event (match_id, minute, type, added_time) "
                     "VALUES (1, 45, 'opponent_goal', 0)"));
}

TEST_F(SchemaTest, LiveMatchHalfLengthAndOneRowPerStep) {
    EXPECT_FALSE(sql("INSERT INTO match_live (match_id, half_length_min) VALUES (1, 9)"));
    EXPECT_FALSE(sql("INSERT INTO match_live (match_id, half_length_min) VALUES (1, 61)"));
    ASSERT_TRUE(sql("INSERT INTO match_live (match_id, half_length_min) VALUES (1, 45)"));
    EXPECT_TRUE(sql("INSERT INTO match_clock_step (match_id, step, at) "
                    "VALUES (1, 'first_half_start', '2026-11-01T14:00:00Z')"));
    EXPECT_FALSE(sql("INSERT INTO match_clock_step (match_id, step, at) "
                     "VALUES (1, 'first_half_start', '2026-11-01T14:01:00Z')"));
    EXPECT_FALSE(sql("INSERT INTO match_clock_step (match_id, step, at) "
                     "VALUES (1, 'kickoff', '2026-11-01T14:01:00Z')"));
}

TEST_F(SchemaTest, DeletingAMatchRemovesItsLiveState) {
    ASSERT_TRUE(sql("INSERT INTO match_live (match_id, half_length_min) VALUES (1, 45)"));
    ASSERT_TRUE(sql("INSERT INTO match_clock_step (match_id, step, at) "
                    "VALUES (1, 'first_half_start', '2026-11-01T14:00:00Z')"));
    ASSERT_TRUE(sql("DELETE FROM match WHERE id = 1"));
    EXPECT_EQ(count("SELECT count(*) FROM match_live"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM match_clock_step"), 0);
}

TEST_F(SchemaTest, MinuteOffCannotPrecedeMinuteOn) {
    EXPECT_FALSE(sql("INSERT INTO appearance (match_id, player_id, minute_on, minute_off) VALUES "
                     "(1, 1, 60, 45)"));
    EXPECT_TRUE(sql("INSERT INTO appearance (match_id, player_id, minute_on, minute_off) VALUES "
                    "(1, 1, 45, 60)"));
}

TEST_F(SchemaTest, MetricValueHasAtMostOneOfMatchAndSession) {
    const char* insert = "INSERT INTO metric_value (metric_id, player_id, match_id, session_id, "
                         "date, value) VALUES (1, 1, ?, ?, '2026-11-01', 5)";
    EXPECT_TRUE(sql(insert, {1, kNull}));
    EXPECT_TRUE(sql(insert, {kNull, 1}));
    EXPECT_TRUE(sql(insert, {kNull, kNull})); // scope "player"
    EXPECT_FALSE(sql(insert, {1, 1}));
}

TEST_F(SchemaTest, MetricDefinitionMinCannotExceedMax) {
    const char* insert = "INSERT INTO metric_definition (key, name_pl, name_en, value_type, "
                         "aggregation, scope, min, max) "
                         "VALUES (?, 'x', 'x', 'decimal', 'avg', 'player', ?, ?)";
    EXPECT_TRUE(sql(insert, {QStringLiteral("a"), 0.0, 10.0}));
    EXPECT_TRUE(sql(insert, {QStringLiteral("b"), kNull, 10.0}));
    EXPECT_FALSE(sql(insert, {QStringLiteral("c"), 10.0, 0.0}));
    EXPECT_FALSE(sql(insert, {QStringLiteral("a"), 0.0, 10.0})); // key is unique
}

TEST_F(SchemaTest, LineupBelongsToAMatchOrIsATemplate) {
    const char* insert = "INSERT INTO lineup (match_id, template_name, formation, tactic_json) "
                         "VALUES (?, ?, '4-3-3', '{}')";
    EXPECT_TRUE(sql(insert, {1, kNull}));
    EXPECT_TRUE(sql(insert, {kNull, QStringLiteral("Podstawowa")}));
    EXPECT_FALSE(sql(insert, {kNull, kNull}));
    EXPECT_FALSE(sql(insert, {1, QStringLiteral("Podstawowa")}));
}

TEST_F(SchemaTest, SchemaVersionHoldsOneRow) {
    EXPECT_FALSE(sql("INSERT INTO schema_version (id, version) VALUES (2, 5)"));
}

// --- Declared types ---

TEST_F(SchemaTest, SqliteIsNewEnoughForStrictTables) {
    const QString version =
        scalar(m_manager->database(), QStringLiteral("SELECT sqlite_version()")).toString();
    EXPECT_TRUE(QVersionNumber::fromString(version) >= QVersionNumber(3, 37))
        << version.toStdString();
}

TEST_F(SchemaTest, EveryTableIsStrict) {
    QSqlQuery query(m_manager->database());
    ASSERT_TRUE(query.prepare(QStringLiteral("SELECT name, strict FROM pragma_table_list "
                                             "WHERE schema = 'main' AND type = 'table' "
                                             "AND name NOT LIKE 'sqlite_%'")));
    ASSERT_TRUE(query.exec());
    int tables = 0;
    while (query.next()) {
        EXPECT_EQ(query.value(1).toInt(), 1) << query.value(0).toString().toStdString();
        ++tables;
    }
    EXPECT_EQ(tables, 21);
}

TEST_F(SchemaTest, NumericColumnsRejectValuesOfTheWrongType) {
    const QVariantList notInteger{1.5, QStringLiteral("junk")};
    const QString insertAppearance =
        QStringLiteral("INSERT INTO appearance (match_id, player_id, %1) VALUES (1, 1, ?)");
    const QString insertAttendance = QStringLiteral(
        "INSERT INTO attendance (session_id, player_id, status, %1) VALUES (1, 1, 'present', ?)");
    const ColumnRule rules[] = {
        {"player.shirt_number", insertPlayerWith("shirt_number"), {8}, notInteger},
        {"player.birth_year", insertPlayerWith("birth_year"), {1980}, notInteger},
        {"appearance.rpe", insertAppearance.arg(QStringLiteral("rpe")), {5}, notInteger},
        {"appearance.rating", insertAppearance.arg(QStringLiteral("rating")), {5}, notInteger},
        {"appearance.minute_on",
         insertAppearance.arg(QStringLiteral("minute_on")),
         {5},
         notInteger},
        {"attendance.rpe", insertAttendance.arg(QStringLiteral("rpe")), {5}, notInteger},
        {"attendance.minutes_override",
         insertAttendance.arg(QStringLiteral("minutes_override")),
         {5},
         notInteger},
        {"match_event.minute",
         QStringLiteral("INSERT INTO match_event (match_id, player_id, type, minute) "
                        "VALUES (1, 1, 'goal', ?)"),
         {5},
         notInteger},
        {"training_session.duration_min",
         QStringLiteral("INSERT INTO training_session (team_id, date, type, duration_min) "
                        "VALUES (1, '2026-11-01', 'gym', ?)"),
         {90},
         notInteger},
        {"player_attribute.rating",
         QStringLiteral("INSERT INTO player_attribute (player_id, attribute, date, rating) "
                        "VALUES (1, 'speed', '2026-11-01', ?)"),
         {5},
         notInteger},
        {"wellness.sleep", insertWellnessWith("sleep"), {3}, notInteger},
        {"staff_user.iterations",
         QStringLiteral("INSERT INTO staff_user (name, role, pin_hash, salt, iterations) "
                        "VALUES ('X', 'coach', x'00', x'00', ?)"),
         {1000},
         notInteger},
        {"metric_value.value",
         QStringLiteral("INSERT INTO metric_value (metric_id, player_id, date, value) "
                        "VALUES (1, 1, '2026-11-01', ?)"),
         {1.5, 5},
         {QStringLiteral("junk")}},
    };
    for (const ColumnRule& rule : rules) {
        checkRule(rule);
    }
}

TEST_F(SchemaTest, LogicalColumnsAcceptOnlyZeroAndOne) {
    const QVariantList accepted{0, 1};
    const QVariantList rejected{2, -1, 1.5};
    const ColumnRule rules[] = {
        {"staff_user.active",
         QStringLiteral("INSERT INTO staff_user (name, role, pin_hash, salt, iterations, active) "
                        "VALUES ('X', 'coach', x'00', x'00', 1, ?)"),
         accepted, rejected},
        {"appearance.started",
         QStringLiteral("INSERT INTO appearance (match_id, player_id, started) VALUES (1, 1, ?)"),
         accepted, rejected},
        {"metric_definition.higher_is_better",
         QStringLiteral("INSERT INTO metric_definition (key, name_pl, name_en, value_type, "
                        "aggregation, scope, higher_is_better) "
                        "VALUES ('k', 'x', 'x', 'integer', 'sum', 'match', ?)"),
         accepted, rejected},
    };
    for (const ColumnRule& rule : rules) {
        checkRule(rule);
    }
}

// --- Values of single columns ---

TEST_F(SchemaTest, EnumerationsAcceptExactlyTheSpecValues) {
    const QVariantList positions = texts(
        {"gk", "rb", "cb", "lb", "rwb", "lwb", "dm", "cm", "am", "rm", "lm", "rw", "lw", "st"});
    const ColumnRule rules[] = {
        {"team.category",
         QStringLiteral("INSERT INTO team (club_id, name, season, category) "
                        "VALUES (1, 'X', '2026/27', ?)"),
         texts({"senior", "u19", "u17", "u15", "u13", "u11", "other"}),
         texts({"U-19", "Senior", ""})},
        {"staff_user.role",
         QStringLiteral("INSERT INTO staff_user (name, pin_hash, salt, iterations, role) "
                        "VALUES ('X', x'00', x'00', 1, ?)"),
         texts({"coach", "assistant", "physio", "analyst"}), texts({"Coach", "admin", ""})},
        {"player.primary_position",
         QStringLiteral("INSERT INTO player (team_id, first_name, last_name, primary_position) "
                        "VALUES (1, 'Adam', 'Nowak', ?)"),
         positions, texts({"GK", "xx", ""})},
        {"player.foot", insertPlayerWith("foot"), texts({"left", "right", "both", "unknown"}),
         texts({"Left", "lewa"})},
        {"availability.type",
         QStringLiteral("INSERT INTO availability (player_id, from_date, type) "
                        "VALUES (1, '2026-11-01', ?)"),
         texts({"injury", "illness", "suspension", "absence"}), texts({"Injury", "holiday"})},
        {"match.venue",
         QStringLiteral("INSERT INTO match (team_id, opponent_id, date, venue) "
                        "VALUES (1, 1, '2026-11-01', ?)"),
         texts({"home", "away", "neutral"}), texts({"Home", "stadium"})},
        {"match.status",
         QStringLiteral("INSERT INTO match (team_id, opponent_id, date, venue, status) "
                        "VALUES (1, 1, '2026-11-01', 'home', ?)"),
         texts({"planned", "live", "finished"}), texts({"Planned", "cancelled"})},
        {"appearance.position",
         QStringLiteral("INSERT INTO appearance (match_id, player_id, position) VALUES (1, 1, ?)"),
         positions + QVariantList{kNull}, texts({"GK", "xx"})},
        {"match_event.type",
         QStringLiteral("INSERT INTO match_event (match_id, minute, player_id, type) "
                        "VALUES (1, 10, 1, ?)"),
         texts({"goal", "assist", "yellow", "red", "sub", "opponent_goal"}),
         texts({"Goal", "own_goal"})},
        {"training_session.type",
         QStringLiteral("INSERT INTO training_session (team_id, date, duration_min, type) "
                        "VALUES (1, '2026-11-01', 90, ?)"),
         texts({"training", "match_prep", "recovery", "gym", "other"}), texts({"Gym", "rest"})},
        {"attendance.status",
         QStringLiteral("INSERT INTO attendance (session_id, player_id, status) VALUES (1, 1, ?)"),
         texts({"present", "absent", "excused", "injured"}), texts({"Present", "late"})},
        {"metric_definition.value_type",
         QStringLiteral("INSERT INTO metric_definition (key, name_pl, name_en, aggregation, scope, "
                        "value_type) VALUES ('k', 'x', 'x', 'sum', 'match', ?)"),
         texts({"integer", "decimal", "boolean"}), texts({"text", "Integer"})},
        {"metric_definition.aggregation",
         QStringLiteral("INSERT INTO metric_definition (key, name_pl, name_en, value_type, scope, "
                        "aggregation) VALUES ('k', 'x', 'x', 'integer', 'match', ?)"),
         texts({"sum", "avg", "min", "max", "last"}), texts({"median", "Sum"})},
        {"metric_definition.scope",
         QStringLiteral("INSERT INTO metric_definition (key, name_pl, name_en, value_type, "
                        "aggregation, scope) VALUES ('k', 'x', 'x', 'integer', 'sum', ?)"),
         texts({"match", "training", "player"}), texts({"team", "Match"})},
    };
    for (const ColumnRule& rule : rules) {
        checkRule(rule);
    }
}

TEST_F(SchemaTest, RangesAcceptTheirBoundsAndRejectTheNextValue) {
    const QString insertAppearance =
        QStringLiteral("INSERT INTO appearance (match_id, player_id, %1) VALUES (1, 1, ?)");
    const QString insertAttendance = QStringLiteral(
        "INSERT INTO attendance (session_id, player_id, status, %1) VALUES (1, 1, 'present', ?)");
    const ColumnRule rules[] = {
        {"player.shirt_number", insertPlayerWith("shirt_number"), {1, 99, kNull}, {0, 100, -1}},
        {"player.birth_year", insertPlayerWith("birth_year"), {1950, 2020, kNull}, {1949, 0}},
        {"appearance.rating",
         insertAppearance.arg(QStringLiteral("rating")),
         {1, 10, kNull},
         {0, 11}},
        {"appearance.rpe", insertAppearance.arg(QStringLiteral("rpe")), {0, 10, kNull}, {-1, 11}},
        {"appearance.minute_on",
         insertAppearance.arg(QStringLiteral("minute_on")),
         {0, 130},
         {-1, 131}},
        {"appearance.minute_off",
         insertAppearance.arg(QStringLiteral("minute_off")),
         {0, 130},
         {-1, 131}},
        {"appearance.started", insertAppearance.arg(QStringLiteral("started")), {0, 1}, {2, -1}},
        {"match_event.minute",
         QStringLiteral("INSERT INTO match_event (match_id, player_id, type, minute) "
                        "VALUES (1, 1, 'goal', ?)"),
         {0, 130},
         {-1, 131}},
        {"training_session.duration_min",
         QStringLiteral("INSERT INTO training_session (team_id, date, type, duration_min) "
                        "VALUES (1, '2026-11-01', 'gym', ?)"),
         {15, 240},
         {14, 241}},
        {"attendance.rpe", insertAttendance.arg(QStringLiteral("rpe")), {0, 10, kNull}, {-1, 11}},
        {"attendance.minutes_override",
         insertAttendance.arg(QStringLiteral("minutes_override")),
         {0, 240, kNull},
         {-1, 241}},
        {"player_attribute.rating",
         QStringLiteral("INSERT INTO player_attribute (player_id, attribute, date, rating) "
                        "VALUES (1, 'speed', '2026-11-01', ?)"),
         {1, 10},
         {0, 11}},
        {"wellness.sleep", insertWellnessWith("sleep"), {1, 5}, {0, 6}},
        {"wellness.fatigue", insertWellnessWith("fatigue"), {1, 5}, {0, 6}},
        {"wellness.soreness", insertWellnessWith("soreness"), {1, 5}, {0, 6}},
        {"wellness.stress", insertWellnessWith("stress"), {1, 5}, {0, 6}},
        {"wellness.mood", insertWellnessWith("mood"), {1, 5}, {0, 6}},
    };
    for (const ColumnRule& rule : rules) {
        checkRule(rule);
    }
}

TEST_F(SchemaTest, NamesMustBeOneToFiftyCharactersAfterTrimming) {
    const QString fifty(50, u'ą');
    const QString fiftyOne(51, u'ą');
    const QString padded = QStringLiteral("  ") + fifty + QStringLiteral("  ");
    checkRule(
        {"player.first_name",
         QStringLiteral("INSERT INTO player (team_id, last_name, primary_position, first_name) "
                        "VALUES (1, 'Nowak', 'st', ?)"),
         {QStringLiteral("A"), fifty, padded},
         {QString{}, QStringLiteral("   "), fiftyOne}});
    checkRule(
        {"player.last_name",
         QStringLiteral("INSERT INTO player (team_id, first_name, primary_position, last_name) "
                        "VALUES (1, 'Adam', 'st', ?)"),
         {QStringLiteral("A"), fifty},
         {QString{}, QStringLiteral("   "), fiftyOne}});
}

TEST_F(SchemaTest, OpenValueColumnsOnlyMustNotBeEmpty) {
    checkRule({"import_log.type",
               QStringLiteral("INSERT INTO import_log (timestamp, file, hash, type) "
                              "VALUES ('2026-11-01T10:00:00Z', 'a.csv', 'abc', ?)"),
               texts({"squad", "anything_later"}), texts({""})});
    checkRule({"event_data_file.provider",
               QStringLiteral("INSERT INTO event_data_file (match_id, path, hash, provider) "
                              "VALUES (1, 'a.json', 'abc', ?)"),
               texts({"statsbomb", "anything_later"}), texts({""})});
    checkRule({"player_attribute.attribute",
               QStringLiteral("INSERT INTO player_attribute (player_id, rating, date, attribute) "
                              "VALUES (1, 5, '2026-11-01', ?)"),
               texts({"speed", "anything_later"}), texts({""})});
}

TEST_F(SchemaTest, SeasonFormatIsChecked) {
    checkRule({"team.season",
               QStringLiteral("INSERT INTO team (club_id, name, category, season) "
                              "VALUES (1, 'X', 'senior', ?)"),
               texts({"2026/27", "1999/00"}),
               texts({"2026", "26/27", "2026-27", "2026/2027", ""})});
}

TEST_F(SchemaTest, DatesMustBeRealIsoDates) {
    const QVariantList bad = texts(
        {"2026-02-30", "2026-13-01", "01.11.2026", "2026-1-1", "soon", "2026-11-01T10:00:00Z", ""});
    const QVariantList good = texts({"2026-11-01", "2024-02-29"});
    const ColumnRule rules[] = {
        {"match.date",
         QStringLiteral(
             "INSERT INTO match (team_id, opponent_id, venue, date) VALUES (1, 1, 'home', ?)"),
         good, bad},
        {"training_session.date",
         QStringLiteral("INSERT INTO training_session (team_id, duration_min, type, date) "
                        "VALUES (1, 90, 'gym', ?)"),
         good, bad},
        {"availability.from_date",
         QStringLiteral(
             "INSERT INTO availability (player_id, type, from_date) VALUES (1, 'injury', ?)"),
         good, bad},
        {"player.archived_at", insertPlayerWith("archived_at"), good + QVariantList{kNull}, bad},
    };
    for (const ColumnRule& rule : rules) {
        checkRule(rule);
    }
}

TEST_F(SchemaTest, TimesOfDayMustBeHoursAndMinutes) {
    const QVariantList good = texts({"00:00", "18:30", "23:59"}) + QVariantList{kNull};
    const QVariantList bad = texts({"24:00", "25:00", "18:60", "6:30", "18:30:00", "soon", ""});
    checkRule({"match.kickoff_time",
               QStringLiteral("INSERT INTO match (team_id, opponent_id, date, venue, kickoff_time) "
                              "VALUES (1, 1, '2026-11-01', 'home', ?)"),
               good, bad});
    checkRule({"training_session.start_time",
               QStringLiteral("INSERT INTO training_session (team_id, date, duration_min, type, "
                              "start_time) VALUES (1, '2026-11-01', 90, 'gym', ?)"),
               good, bad});
}
