#include "data/repositories/RepositorySupport.hpp"

#include "RepositoryFixture.hpp"
#include "data/db/Transaction.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

class RepositorySupportTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    // The error of a statement that has to fail.
    [[nodiscard]] Error
    failure(const char* statement, const QVariantList& values = {},
            ForeignKeyFailure foreignKeyFailure = ForeignKeyFailure::MissingParent) {
        const auto result =
            runSql(*m_db, QString::fromUtf8(statement), values, "test", foreignKeyFailure);
        EXPECT_FALSE(result.has_value()) << statement;
        return result ? Error{} : result.error();
    }
};

class RepositorySupportFileTest : public TempDatabaseTest {};

} // namespace

// --- ISO text ---

TEST(IsoTextTest, DateRoundTrips) {
    const Date date = 2026y / November / 1d;
    EXPECT_EQ(toIsoDate(date), QStringLiteral("2026-11-01"));
    const auto parsed = parseIsoDate(QStringLiteral("2026-11-01"));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, date);
    EXPECT_EQ(parseIsoDate(QStringLiteral("2024-02-29")).value(), 2024y / February / 29d);
}

TEST(IsoTextTest, ImpossibleDateStaysVisibleInsteadOfBecomingNull) {
    EXPECT_EQ(toIsoDate(2026y / February / 30d), QStringLiteral("2026-02-30"));
}

TEST(IsoTextTest, DamagedDateFails) {
    for (const char* text :
         {"", "soon", "2026-02-30", "2026-1-1", "01.11.2026", "2026-11-01T10:00:00Z"}) {
        const auto parsed = parseIsoDate(QString::fromLatin1(text));
        ASSERT_FALSE(parsed.has_value()) << text;
        EXPECT_EQ(parsed.error().code, ErrorCode::Database) << text;
    }
}

TEST(IsoTextTest, TimeRoundTrips) {
    EXPECT_EQ(toIsoTime(minutes{0}), QStringLiteral("00:00"));
    EXPECT_EQ(toIsoTime(minutes{18 * 60 + 30}), QStringLiteral("18:30"));
    EXPECT_EQ(toIsoTime(minutes{1439}), QStringLiteral("23:59"));
    EXPECT_EQ(parseIsoTime(QStringLiteral("00:00")).value(), minutes{0});
    EXPECT_EQ(parseIsoTime(QStringLiteral("18:30")).value(), minutes{18 * 60 + 30});
    EXPECT_EQ(parseIsoTime(QStringLiteral("23:59")).value(), minutes{1439});
}

TEST(IsoTextTest, DamagedTimeFails) {
    for (const char* text : {"", "soon", "24:00", "18:60", "6:30", "18:30:00"}) {
        const auto parsed = parseIsoTime(QString::fromLatin1(text));
        ASSERT_FALSE(parsed.has_value()) << text;
        EXPECT_EQ(parsed.error().code, ErrorCode::Database) << text;
    }
}

TEST(IsoTextTest, TimestampRoundTripsInUtc) {
    const Timestamp timestamp = sys_days{2026y / November / 1d} + hours{10} + seconds{30};
    EXPECT_EQ(toIsoTimestamp(timestamp), QStringLiteral("2026-11-01T10:00:30Z"));
    const auto parsed = parseIsoTimestamp(QStringLiteral("2026-11-01T10:00:30Z"));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, timestamp);
}

TEST(IsoTextTest, DamagedTimestampFails) {
    for (const char* text : {"", "soon", "2026-13-01T10:00:00Z"}) {
        const auto parsed = parseIsoTimestamp(QString::fromLatin1(text));
        ASSERT_FALSE(parsed.has_value()) << text;
        EXPECT_EQ(parsed.error().code, ErrorCode::Database) << text;
    }
}

// --- Constraint violations ---

TEST_F(RepositorySupportTest, DuplicateInAPartialIndexIsAConflictOnTheLastColumn) {
    const Error error = failure("INSERT INTO player (team_id, first_name, last_name, shirt_number, "
                                "primary_position) VALUES (1, 'Piotr', 'Lis', 7, 'gk')");
    EXPECT_EQ(error.code, ErrorCode::Conflict) << error.detail;
    EXPECT_EQ(error.field, "shirt_number") << error.detail;
}

TEST_F(RepositorySupportTest, DuplicateInAUniqueColumnIsAConflictOnThatColumn) {
    const Error error = failure("INSERT INTO opponent (name, name_key) VALUES ('RYWAL', 'rywal')");
    EXPECT_EQ(error.code, ErrorCode::Conflict) << error.detail;
    EXPECT_EQ(error.field, "name_key") << error.detail;
}

TEST_F(RepositorySupportTest, DuplicateInATableConstraintIsAConflictOnTheLastColumn) {
    ASSERT_TRUE(sql("INSERT INTO attendance (session_id, player_id, status) "
                    "VALUES (1, 1, 'absent')"));
    const Error error = failure("INSERT INTO attendance (session_id, player_id, status) "
                                "VALUES (1, 1, 'absent')");
    EXPECT_EQ(error.code, ErrorCode::Conflict) << error.detail;
    EXPECT_EQ(error.field, "player_id") << error.detail;
}

TEST_F(RepositorySupportTest, DuplicatePrimaryKeyIsAConflict) {
    const Error error = failure("INSERT INTO club (id, name) VALUES (1, 'Inny')");
    EXPECT_EQ(error.code, ErrorCode::Conflict) << error.detail;
    EXPECT_EQ(error.field, "id") << error.detail;
}

TEST_F(RepositorySupportTest, MissingParentIsInvalidInput) {
    const Error error = failure("INSERT INTO player (team_id, first_name, last_name, "
                                "primary_position) VALUES (999, 'Piotr', 'Lis', 'gk')");
    EXPECT_EQ(error.code, ErrorCode::InvalidInput) << error.detail;
}

TEST_F(RepositorySupportTest, RowInUseIsAConflict) {
    const Error error = failure("DELETE FROM team WHERE id = 1", {}, ForeignKeyFailure::RowInUse);
    EXPECT_EQ(error.code, ErrorCode::Conflict) << error.detail;
}

TEST_F(RepositorySupportTest, NotNullViolationIsInvalidInputOnThatColumn) {
    const Error error = failure("INSERT INTO club (name) VALUES (NULL)");
    EXPECT_EQ(error.code, ErrorCode::InvalidInput) << error.detail;
    EXPECT_EQ(error.field, "name") << error.detail;
}

TEST_F(RepositorySupportTest, CheckViolationIsInvalidInput) {
    const Error error = failure("INSERT INTO player (team_id, first_name, last_name, shirt_number, "
                                "primary_position) VALUES (1, 'Piotr', 'Lis', 100, 'gk')");
    EXPECT_EQ(error.code, ErrorCode::InvalidInput) << error.detail;
}

TEST_F(RepositorySupportTest, ValueOfTheWrongTypeIsInvalidInput) {
    const Error error = failure("INSERT INTO player (team_id, first_name, last_name, shirt_number, "
                                "primary_position) VALUES (1, 'Piotr', 'Lis', 'junk', 'gk')");
    EXPECT_EQ(error.code, ErrorCode::InvalidInput) << error.detail;
}

TEST_F(RepositorySupportTest, InvalidSqlStaysADatabaseError) {
    const Error error = failure("THIS IS NOT SQL");
    EXPECT_EQ(error.code, ErrorCode::Database) << error.detail;
    EXPECT_TRUE(error.field.empty());
}

TEST_F(RepositorySupportTest, ConstraintViolationKeepsTheTransactionUsable) {
    auto transaction = Transaction::begin(*m_db);
    ASSERT_TRUE(transaction.has_value());
    ASSERT_FALSE(
        runSql(*m_db, QStringLiteral("INSERT INTO club (id, name) VALUES (1, 'Inny')"), {}, "test")
            .has_value());
    EXPECT_TRUE(m_db->execSql(QStringLiteral("DELETE FROM attendance"), "test").has_value());
    EXPECT_TRUE(transaction->commit().has_value());
}

// The check of AGENTS section 7: statementError() has to reach DatabaseManager::sqlError().
TEST_F(RepositorySupportTest, ErrorThatMayLoseTheTransactionMarksItAsBroken) {
    auto transaction = Transaction::begin(*m_db);
    ASSERT_TRUE(transaction.has_value());
    const QSqlError busy(QStringLiteral("driver text"), QStringLiteral("database text"),
                         QSqlError::StatementError, QStringLiteral("5"));
    EXPECT_EQ(statementError(*m_db, busy, "test").code, ErrorCode::DatabaseLocked);
    EXPECT_FALSE(m_db->execSql(QStringLiteral("DELETE FROM attendance"), "test").has_value());
    EXPECT_FALSE(transaction->commit().has_value());
}

TEST_F(RepositorySupportFileTest, DatabaseHeldByAnotherProcessIsReportedAsLocked) {
    auto opened = DatabaseManager::open(databasePath(), options());
    ASSERT_TRUE(opened.has_value()) << opened.error().detail;
    RawConnection other(databasePath());
    ASSERT_TRUE(run(other.database(), QStringLiteral("BEGIN IMMEDIATE")));

    const auto result = insertRow(**opened, QStringLiteral("INSERT INTO club (name) VALUES (?)"),
                                  {QStringLiteral("KS Test")}, "test");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::DatabaseLocked) << result.error().detail;

    ASSERT_TRUE(run(other.database(), QStringLiteral("ROLLBACK")));
}

// --- Statements ---

TEST_F(RepositorySupportTest, InsertRowReturnsTheNewId) {
    const auto id = insertRow(*m_db,
                              QStringLiteral("INSERT INTO opponent (name, name_key) "
                                             "VALUES (?, ?)"),
                              {QStringLiteral("Orzeł"), QStringLiteral("orzeł")}, "test");
    ASSERT_TRUE(id.has_value()) << id.error().detail;
    EXPECT_EQ(*id, 2);
}

TEST_F(RepositorySupportTest, ChangeRowWithoutAMatchIsNotFound) {
    const QString update = QStringLiteral("UPDATE opponent SET notes = 'x' WHERE id = ?");
    EXPECT_TRUE(changeRow(*m_db, update, {1}, "test").has_value());
    const auto missing = changeRow(*m_db, update, {999}, "test");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST_F(RepositorySupportTest, ChangeRowKeepsItsForeignKeyMeaning) {
    const auto inUse = changeRow(*m_db, QStringLiteral("DELETE FROM team WHERE id = ?"), {1},
                                 "test", ForeignKeyFailure::RowInUse);
    ASSERT_FALSE(inUse.has_value());
    EXPECT_EQ(inUse.error().code, ErrorCode::Conflict);
}

TEST_F(RepositorySupportTest, RequireRowTellsWhetherTheRowExists) {
    const QString select = QStringLiteral("SELECT 1 FROM opponent WHERE id = ?");
    EXPECT_TRUE(requireRow(*m_db, select, {1}, "test").has_value());
    const auto missing = requireRow(*m_db, select, {999}, "test");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST_F(RepositorySupportTest, SelectOneWithoutARowIsNotFound) {
    const auto row = selectOne(*m_db, QStringLiteral("SELECT name FROM club WHERE id = 999"), {},
                               "test", [](RowReader& reader) { return reader.text(); });
    ASSERT_FALSE(row.has_value());
    EXPECT_EQ(row.error().code, ErrorCode::NotFound);
}

TEST_F(RepositorySupportTest, SelectOneReadsTheRow) {
    const auto row = selectOne(*m_db, QStringLiteral("SELECT name FROM club WHERE id = 1"), {},
                               "test", [](RowReader& reader) { return reader.text(); });
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(*row, "KS Test");
}

TEST_F(RepositorySupportTest, SelectAllReturnsEveryRowInOrder) {
    const auto rows = selectAll(*m_db, QStringLiteral("SELECT id FROM team ORDER BY id DESC"), {},
                                "test", [](RowReader& reader) { return reader.id<TeamId>(); });
    ASSERT_TRUE(rows.has_value());
    EXPECT_EQ(*rows, (std::vector{TeamId{2}, TeamId{1}}));
}

TEST_F(RepositorySupportTest, SelectAllWithNoRowsIsAnEmptyList) {
    const auto rows = selectAll(*m_db, QStringLiteral("SELECT id FROM team WHERE id = 999"), {},
                                "test", [](RowReader& reader) { return reader.integer(); });
    ASSERT_TRUE(rows.has_value());
    EXPECT_TRUE(rows->empty());
}

TEST_F(RepositorySupportTest, UnreadableValueInARowFailsTheWholeResult) {
    ASSERT_TRUE(sql("UPDATE player SET secondary_positions = 'cb,xx' WHERE id = 1"));
    const auto rows = selectAll(*m_db, QStringLiteral("SELECT secondary_positions FROM player"), {},
                                "test", [](RowReader& reader) { return reader.positions(); });
    ASSERT_FALSE(rows.has_value());
    EXPECT_EQ(rows.error().code, ErrorCode::Database);
}

// --- Values to bind ---

TEST_F(RepositorySupportTest, EmptyTextIsBoundAsTextNotNull) {
    const auto typeOf = [this](const QVariant& value) {
        return scalar(m_db->database(), QStringLiteral("SELECT typeof(?)"), {value}).toString();
    };
    // The reason textValue() exists.
    EXPECT_EQ(typeOf(QString{}), QStringLiteral("null"));

    EXPECT_EQ(typeOf(textValue("")), QStringLiteral("text"));
    EXPECT_EQ(typeOf(textValue(std::string{})), QStringLiteral("text"));
    EXPECT_EQ(typeOf(textValue(std::string_view{})), QStringLiteral("text"));
}

TEST_F(RepositorySupportTest, NamesAreBoundTrimmed) {
    EXPECT_EQ(nameValue("  Śląsk \n").toString(), QStringLiteral("Śląsk"));
}

TEST_F(RepositorySupportTest, OptionalTextIsTrimmedAndEmptyBecomesNull) {
    const auto typeOf = [this](const std::optional<std::string>& text) {
        return scalar(m_db->database(), QStringLiteral("SELECT typeof(?)"),
                      {optionalTextValue(text)})
            .toString();
    };
    EXPECT_EQ(typeOf(std::nullopt), QStringLiteral("null"));
    EXPECT_EQ(typeOf(""), QStringLiteral("null"));
    EXPECT_EQ(typeOf(" \t "), QStringLiteral("null"));
    EXPECT_EQ(typeOf("uraz"), QStringLiteral("text"));
    EXPECT_EQ(optionalTextValue(" uraz ").toString(), QStringLiteral("uraz"));
}

TEST_F(RepositorySupportTest, LargeIdsSurviveBindingAndReading) {
    const OpponentId big{std::int64_t{1} << 40};
    ASSERT_TRUE(sql("INSERT INTO opponent (id, name, name_key) VALUES (?, 'Duży', 'duży')",
                    {idValue(big)}));
    const auto id =
        selectOne(*m_db, QStringLiteral("SELECT id FROM opponent WHERE id = ?"), {idValue(big)},
                  "test", [](RowReader& reader) { return reader.id<OpponentId>(); });
    ASSERT_TRUE(id.has_value()) << id.error().detail;
    EXPECT_EQ(*id, big);
}

TEST_F(RepositorySupportTest, BytesAreBoundAsBlob) {
    const std::vector<std::uint8_t> bytes{0x00, 0xFF, 0x10};
    const QVariantList values{bytesValue(bytes)};
    EXPECT_EQ(scalar(m_db->database(), QStringLiteral("SELECT typeof(?)"), values).toString(),
              QStringLiteral("blob"));
    const auto read = selectOne(*m_db, QStringLiteral("SELECT ?"), values, "test",
                                [](RowReader& reader) { return reader.bytes(); });
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, bytes);
}

TEST_F(RepositorySupportTest, NullsAreBoundForEmptyOptionals) {
    const auto typeOf = [this](const QVariant& value) {
        return scalar(m_db->database(), QStringLiteral("SELECT typeof(?)"), {value}).toString();
    };
    const QString null = QStringLiteral("null");
    EXPECT_EQ(typeOf(optionalValue(std::nullopt)), null);
    EXPECT_EQ(typeOf(optionalDateValue(std::nullopt)), null);
    EXPECT_EQ(typeOf(optionalTimeValue(std::nullopt)), null);
    EXPECT_EQ(typeOf(optionalTimestampValue(std::nullopt)), null);
    EXPECT_EQ(typeOf(optionalIdValue<PlayerId>(std::nullopt)), null);
    EXPECT_EQ(typeOf(optionalEnumValue<Position>(std::nullopt)), null);

    EXPECT_EQ(typeOf(optionalValue(0)), QStringLiteral("integer"));
    EXPECT_EQ(typeOf(optionalIdValue(std::optional(PlayerId{1}))), QStringLiteral("integer"));
    EXPECT_EQ(typeOf(optionalEnumValue(std::optional(Position::Gk))), QStringLiteral("text"));
}

// --- RowReader ---

TEST_F(RepositorySupportTest, RowReaderReadsColumnsFromLeftToRight) {
    struct Row {
        std::string text;
        std::optional<std::string> noText;
        int integer = 0;
        std::optional<int> noInteger;
        bool flag = false;
        Date date{};
        std::optional<Date> noDate;
        std::optional<TimeOfDay> time;
        Position position = Position::Cm;
        std::optional<Position> noPosition;
        std::optional<PlayerId> player;
    };
    const auto row = selectOne(
        *m_db,
        QStringLiteral("SELECT 'a', NULL, 5, NULL, 1, '2026-11-01', NULL, '18:30', 'gk', NULL, 7"),
        {}, "test", [](RowReader& reader) {
            Row result;
            result.text = reader.text();
            result.noText = reader.optionalText();
            result.integer = reader.integer();
            result.noInteger = reader.optionalInteger();
            result.flag = reader.boolean();
            result.date = reader.date();
            result.noDate = reader.optionalDate();
            result.time = reader.optionalTime();
            result.position = reader.enumeration<Position>();
            result.noPosition = reader.optionalEnumeration<Position>();
            result.player = reader.optionalId<PlayerId>();
            return result;
        });
    ASSERT_TRUE(row.has_value()) << row.error().detail;
    EXPECT_EQ(row->text, "a");
    EXPECT_EQ(row->noText, std::nullopt);
    EXPECT_EQ(row->integer, 5);
    EXPECT_EQ(row->noInteger, std::nullopt);
    EXPECT_TRUE(row->flag);
    EXPECT_EQ(row->date, 2026y / November / 1d);
    EXPECT_EQ(row->noDate, std::nullopt);
    EXPECT_EQ(row->time, minutes{18 * 60 + 30});
    EXPECT_EQ(row->position, Position::Gk);
    EXPECT_EQ(row->noPosition, std::nullopt);
    EXPECT_EQ(row->player, PlayerId{7});
}

TEST_F(RepositorySupportTest, RowReaderKeepsTheFirstErrorAsADatabaseError) {
    const auto row = selectOne(*m_db, QStringLiteral("SELECT 'not-a-position', 'not-a-date'"), {},
                               "test", [](RowReader& reader) {
                                   const Position position = reader.enumeration<Position>();
                                   const Date date = reader.date();
                                   return std::pair{position, date};
                               });
    ASSERT_FALSE(row.has_value());
    EXPECT_EQ(row.error().code, ErrorCode::Database);
    EXPECT_NE(row.error().detail.find("not-a-position"), std::string::npos) << row.error().detail;
}
