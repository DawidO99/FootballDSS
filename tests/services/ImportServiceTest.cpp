#include "services/import/ImportService.hpp"

#include "../data/TestDatabase.hpp"
#include "FakeClock.hpp"
#include "core/domain/StaffUser.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/repositories/ImportLogRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "services/AuthService.hpp"
#include "services/Session.hpp"
#include "xlsxdocument.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace fdss;
using namespace fdss::core;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

constexpr TeamId kTeam{1};

QString fixture(const char* name) {
    return QStringLiteral(FDSS_IMPORT_FIXTURES_DIR "/%1").arg(QLatin1StringView(name));
}

QString templateFile(const char* name) {
    return QStringLiteral(FDSS_IMPORT_TEMPLATES_DIR "/%1").arg(QLatin1StringView(name));
}

template <class T>
void expectError(const Result<T>& result, ErrorCode code, std::string_view field) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, code) << result.error().detail;
    EXPECT_EQ(result.error().field, field) << result.error().detail;
}

// A file database, so commits make real backups and another connection can lock it.
class ImportServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_dir.isValid());
        data::OpenOptions options;
        options.busyTimeoutMs = 100;
        options.backupDirectory = backupDirectory();
        auto opened = data::DatabaseManager::open(databasePath(), options);
        ASSERT_TRUE(opened.has_value()) << opened.error().detail;
        m_db = std::move(*opened);
        ASSERT_TRUE(
            run(m_db->database(), QStringLiteral("INSERT INTO club (id, name) VALUES (1, 'KS')")));
        ASSERT_TRUE(run(m_db->database(),
                        QStringLiteral("INSERT INTO team (id, club_id, name, category, season) "
                                       "VALUES (1, 1, 'Seniorzy', 'senior', '2026/27')")));
        m_staff = std::make_unique<data::StaffRepository>(*m_db);
        m_auth = std::make_unique<AuthService>(*m_db, *m_staff, m_session, m_clock, 4);
        m_log = std::make_unique<data::ImportLogRepository>(*m_db);
        m_players = std::make_unique<data::PlayerRepository>(*m_db);
        m_service = std::make_unique<ImportService>(*m_db, *m_log, *m_players, m_session, m_clock);
        logInAs(Role::Coach);
    }

    [[nodiscard]] QString databasePath() const {
        return m_dir.filePath(QStringLiteral("club.sqlite"));
    }
    [[nodiscard]] QString backupDirectory() const {
        return m_dir.filePath(QStringLiteral("backups"));
    }

    void logInAs(Role role) {
        StaffUserDraft draft;
        draft.name = role == Role::Coach ? "Trener" : "Fizjo";
        draft.role = role;
        const auto hash = m_auth->hashPin("1234");
        ASSERT_TRUE(hash.has_value());
        const auto id = m_staff->create(draft, *hash);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
        const auto login = m_auth->login(*id, "1234");
        ASSERT_TRUE(login.has_value());
        ASSERT_EQ(login->status, PinCheckStatus::Accepted);
    }

    ImportSource read(const QString& path, std::optional<int> sheet = std::nullopt) {
        auto future = m_service->read(path, sheet);
        EXPECT_TRUE(future.has_value());
        if (!future) {
            return {};
        }
        auto source = future->result();
        EXPECT_TRUE(source.has_value()) << source.error().detail;
        return source.value_or(ImportSource{});
    }

    std::vector<SquadRow> validate(const ImportSource& source, const SquadMapping& mapping) {
        auto future = m_service->validateSquad(kTeam, source, mapping);
        EXPECT_TRUE(future.has_value());
        return future ? future->result() : std::vector<SquadRow>{};
    }

    SquadMapping suggested(const ImportSource& source) {
        auto suggestion = m_service->suggestSquadMapping(source);
        EXPECT_TRUE(suggestion.has_value());
        return suggestion ? suggestion->mapping : SquadMapping{};
    }

    // Read, automatic mapping, validation and commit.
    Result<SquadImportSummary> importFile(const QString& path, SquadImportOptions options = {},
                                          std::optional<Position> defaultPosition = std::nullopt) {
        const ImportSource source = read(path);
        SquadMapping mapping = suggested(source);
        if (defaultPosition) {
            mapping.defaultPosition = defaultPosition;
        }
        auto rows = validate(source, mapping);
        return m_service->commitSquad(kTeam, source, mapping, rows, options);
    }

    [[nodiscard]] int count(const char* sql) const {
        return scalar(m_db->database(), QString::fromUtf8(sql)).toInt();
    }

    std::vector<Player> players() {
        auto list = m_players->list(kTeam, data::ArchivedPlayers::Include);
        EXPECT_TRUE(list.has_value());
        return list.value_or(std::vector<Player>{});
    }

    PlayerId addPlayer(const char* first, const char* last, std::optional<int> number) {
        PlayerDraft draft;
        draft.firstName = first;
        draft.lastName = last;
        draft.shirtNumber = number;
        const auto id = m_players->create(kTeam, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(PlayerId{});
    }

    QTemporaryDir m_dir;
    std::unique_ptr<data::DatabaseManager> m_db;
    FakeClock m_clock;
    Session m_session;
    std::unique_ptr<data::StaffRepository> m_staff;
    std::unique_ptr<AuthService> m_auth;
    std::unique_ptr<data::ImportLogRepository> m_log;
    std::unique_ptr<data::PlayerRepository> m_players;
    std::unique_ptr<ImportService> m_service;
};

// --- Detection and reading (SPEC section 8 must-pass cases) ---

TEST(ImportFileTest, VideoRenamedToCsvIsRefused) {
    expectError(readImportFile(fixture("video.csv")), ErrorCode::UnsupportedFormat,
                "import_file_type");
}

TEST(ImportFileTest, EmptyFileAndHeaderOnly) {
    expectError(readImportFile(fixture("empty.csv")), ErrorCode::InvalidInput, "import_empty");
    expectError(readImportFile(fixture("header-only.csv")), ErrorCode::InvalidInput,
                "import_no_rows");
}

TEST(ImportFileTest, XlsJsonAndCorruptedXlsx) {
    expectError(readImportFile(fixture("legacy.xls")), ErrorCode::UnsupportedFormat, "import_xls");
    expectError(readImportFile(fixture("squad.json")), ErrorCode::UnsupportedFormat, "import_json");
    expectError(readImportFile(fixture("corrupted.xlsx")), ErrorCode::UnsupportedFormat,
                "import_xlsx_corrupt");
}

TEST(ImportFileTest, MissingAndTooLargeFiles) {
    expectError(readImportFile(fixture("does-not-exist.csv")), ErrorCode::Io, "import_file");
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString big = dir.filePath(QStringLiteral("big.csv"));
    QFile file(big);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_TRUE(file.resize(kMaxImportFileBytes + 1));
    file.close();
    expectError(readImportFile(big), ErrorCode::InvalidInput, "import_file_size");
}

TEST(ImportFileTest, PolishExcelCsvInCp1250) {
    const auto source = readImportFile(fixture("polish-excel-cp1250.csv"));
    ASSERT_TRUE(source.has_value()) << source.error().detail;
    EXPECT_EQ(source->kind, ImportFileKind::Csv);
    EXPECT_EQ(source->encoding, TextEncoding::Cp1250);
    EXPECT_EQ(source->delimiter, ';');
    EXPECT_EQ(source->table.headers,
              (std::vector<std::string>{"Nazwisko", "Imię", "Nr", "Pozycja"}));
    ASSERT_EQ(source->table.rows.size(), 20U);
    EXPECT_EQ(source->table.rows[0].cells,
              (std::vector<std::string>{"Żółkiewski", "Łukasz", "1", "BR"}));
    EXPECT_EQ(source->fileName, QStringLiteral("polish-excel-cp1250.csv"));
    EXPECT_EQ(source->hash.size(), 64);
}

TEST(ImportFileTest, Utf8WithBom) {
    const auto source = readImportFile(fixture("utf8-bom.csv"));
    ASSERT_TRUE(source.has_value()) << source.error().detail;
    EXPECT_EQ(source->encoding, TextEncoding::Utf8Bom);
    EXPECT_EQ(source->table.headers.front(), "Imię");
}

TEST(ImportFileTest, QuotesAndCommasInsideFields) {
    const auto source = readImportFile(fixture("quotes-commas.csv"));
    ASSERT_TRUE(source.has_value()) << source.error().detail;
    EXPECT_EQ(source->delimiter, ',');
    ASSERT_EQ(source->table.rows.size(), 3U);
    EXPECT_EQ(source->table.rows[0].cells,
              (std::vector<std::string>{"Nowak-Kowalski, Jan", "7", "ŚO, PO",
                                        "Mówi \"cześć\", kapitan"}));
    EXPECT_EQ(source->table.rows[1].cells[3], "wiersz\nw dwóch liniach");
    EXPECT_EQ(source->table.rows[2].number, 5);
}

TEST(ImportFileTest, MultiSheetXlsxTakesTheFirstSheetWithRows) {
    const auto source = readImportFile(fixture("multi-sheet.xlsx"));
    ASSERT_TRUE(source.has_value()) << source.error().detail;
    EXPECT_EQ(source->kind, ImportFileKind::Xlsx);
    EXPECT_EQ(source->sheets, (QStringList{QStringLiteral("Notatki"), QStringLiteral("Kadra")}));
    EXPECT_EQ(source->sheet, 1);
    EXPECT_TRUE(source->tableStatus.has_value());
    EXPECT_EQ(source->table.rows.size(), 3U);
    // The chosen sheet holds a single line: the file is fine, the sheet has no rows.
    const auto notes = readImportFile(fixture("multi-sheet.xlsx"), 0);
    ASSERT_TRUE(notes.has_value()) << notes.error().detail;
    EXPECT_EQ(notes->sheet, 0);
    expectError(notes->tableStatus, ErrorCode::InvalidInput, "import_no_rows");
}

// --- Service ---

TEST_F(ImportServiceTest, NeedsTheImportPermission) {
    m_session.clear();
    expectError(m_service->read(fixture("utf8-bom.csv")), ErrorCode::PermissionDenied, "");
    logInAs(Role::Physio);
    expectError(m_service->read(fixture("utf8-bom.csv")), ErrorCode::PermissionDenied, "");
    expectError(m_service->undo(ImportId{1}), ErrorCode::PermissionDenied, "");
}

// Packet P16, "Done when": the Polish Excel file imports 20 players.
TEST_F(ImportServiceTest, PolishExcelFileImportsTwentyPlayers) {
    const auto summary = importFile(fixture("polish-excel-cp1250.csv"));
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->created, 20);
    EXPECT_EQ(summary->invalid, 0);
    const auto stored = players();
    ASSERT_EQ(stored.size(), 20U);
    EXPECT_EQ(stored[0].lastName, "Żółkiewski");
    EXPECT_EQ(stored[0].firstName, "Łukasz");
    EXPECT_EQ(stored[0].primaryPosition, Position::Gk);
    EXPECT_EQ(stored[19].firstName, "Żelisław");
    EXPECT_EQ(count("SELECT count(*) FROM player WHERE import_id IS NOT NULL"), 20);
    EXPECT_EQ(count("SELECT rows_ok FROM import_log"), 20);
    // Backup before the import.
    EXPECT_FALSE(filesIn(backupDirectory()).isEmpty());
}

TEST_F(ImportServiceTest, QuotedFullNamesInLastFirstOrder) {
    const auto summary = importFile(fixture("quotes-commas.csv"));
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    const auto stored = players();
    ASSERT_EQ(stored.size(), 3U);
    EXPECT_EQ(stored[0].lastName, "Nowak-Kowalski");
    EXPECT_EQ(stored[0].firstName, "Jan");
    EXPECT_EQ(stored[0].primaryPosition, Position::Cb);
    EXPECT_EQ(stored[0].secondaryPositions, (std::vector<Position>{Position::Rb}));
    EXPECT_EQ(stored[1].lastName, "Źrebiec");
    EXPECT_EQ(stored[2].lastName, "Łoś");
}

TEST_F(ImportServiceTest, DuplicatePlayersInTheFileAndInTheTeam) {
    addPlayer("Piotr", "Lis", 11);
    const ImportSource source = read(fixture("duplicates.csv"));
    const SquadMapping mapping = suggested(source);
    auto rows = validate(source, mapping);
    ASSERT_EQ(rows.size(), 5U);
    EXPECT_EQ(rows[0].status(), RowStatus::Error); // Jan Kowalski twice
    EXPECT_EQ(rows[1].status(), RowStatus::Error); // number 9 twice
    EXPECT_EQ(rows[2].match, PlayerMatch::Existing);
    EXPECT_EQ(rows[3].status(), RowStatus::Error);
    EXPECT_EQ(rows[4].status(), RowStatus::Error);
    expectError(m_service->commitSquad(kTeam, source, mapping, rows, {}), ErrorCode::InvalidInput,
                "import_nothing");
    const auto updated = m_service->commitSquad(kTeam, source, mapping, rows,
                                                {ExistingPolicy::Update, InvalidRows::Skip});
    ASSERT_TRUE(updated.has_value()) << updated.error().detail;
    EXPECT_EQ(updated->updated, 1);
    EXPECT_EQ(updated->invalid, 4);
    EXPECT_EQ(count("SELECT count(*) FROM player"), 1);
}

TEST_F(ImportServiceTest, UnknownPositionsSkipOrAbort) {
    const ImportSource source = read(fixture("unknown-positions.csv"));
    const SquadMapping mapping = suggested(source);
    auto rows = validate(source, mapping);
    ASSERT_EQ(rows.size(), 4U);
    EXPECT_EQ(rows[0].status(), RowStatus::Error);
    EXPECT_EQ(rows[1].status(), RowStatus::Warning);
    EXPECT_EQ(rows[2].status(), RowStatus::Error);
    EXPECT_EQ(rows[3].status(), RowStatus::Ok);
    expectError(m_service->commitSquad(kTeam, source, mapping, rows,
                                       {ExistingPolicy::Skip, InvalidRows::Abort}),
                ErrorCode::InvalidInput, "import_has_errors");
    EXPECT_EQ(count("SELECT count(*) FROM player"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM import_log"), 0);
    const auto summary = m_service->commitSquad(kTeam, source, mapping, rows, {});
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->created, 2);
    EXPECT_EQ(count("SELECT rows_failed FROM import_log"), 2);
}

TEST_F(ImportServiceTest, FileWithoutPositionUsesTheDefault) {
    const ImportSource source = read(fixture("no-position.csv"));
    SquadMapping mapping = suggested(source);
    EXPECT_TRUE(needsDefaultPosition(mapping, source.table));
    mapping.defaultPosition = Position::Cm;
    auto rows = validate(source, mapping);
    ASSERT_EQ(rows.size(), 3U);
    for (const SquadRow& row : rows) {
        EXPECT_EQ(row.status(), RowStatus::Warning);
    }
    const auto summary = m_service->commitSquad(kTeam, source, mapping, rows, {});
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->created, 3);
    const auto stored = players();
    EXPECT_TRUE(std::ranges::all_of(
        stored, [](const Player& player) { return player.primaryPosition == Position::Cm; }));
    EXPECT_EQ(stored[2].shirtNumber, std::nullopt);
}

TEST_F(ImportServiceTest, SameFileTwiceIsFound) {
    const ImportSource source = read(fixture("utf8-bom.csv"));
    EXPECT_TRUE(m_service->previousImports(source)->empty());
    const auto first = importFile(fixture("utf8-bom.csv"));
    ASSERT_TRUE(first.has_value()) << first.error().detail;
    const auto previous = m_service->previousImports(read(fixture("utf8-bom.csv")));
    ASSERT_TRUE(previous.has_value());
    ASSERT_EQ(previous->size(), 1U);
    EXPECT_EQ(previous->front().id, first->importId);
    EXPECT_FALSE(previous->front().undone);
    // The second time every row is an existing player.
    expectError(importFile(fixture("utf8-bom.csv")), ErrorCode::InvalidInput, "import_nothing");
    ASSERT_TRUE(m_service->undo(first->importId).has_value());
    EXPECT_TRUE(m_service->previousImports(source)->front().undone);
}

TEST_F(ImportServiceTest, ConfirmedMappingIsOfferedAgain) {
    const ImportSource source = read(fixture("utf8-bom.csv"));
    EXPECT_FALSE(m_service->suggestSquadMapping(source)->remembered);
    SquadMapping mapping = suggested(source);
    mapping.columns[2].reset(); // ignore "Nr"
    auto rows = validate(source, mapping);
    ASSERT_TRUE(m_service->commitSquad(kTeam, source, mapping, rows, {}).has_value());
    const auto again = m_service->suggestSquadMapping(source);
    ASSERT_TRUE(again.has_value());
    EXPECT_TRUE(again->remembered);
    EXPECT_EQ(again->mapping, mapping);
}

TEST_F(ImportServiceTest, UndoDeletesOrArchivesAndKeepsUpdates) {
    const PlayerId existing = addPlayer("Józef", "Ćwiękała", 1);
    const auto summary =
        importFile(fixture("polish-excel-cp1250.csv"), {ExistingPolicy::Update, InvalidRows::Skip});
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->created, 18); // number 1 of Żółkiewski belongs to another player
    EXPECT_EQ(summary->updated, 1);  // Józef Ćwiękała, number 1 -> 2
    EXPECT_EQ(summary->skippedAmbiguous, 1);
    const auto imported = m_players->listByImport(summary->importId);
    ASSERT_TRUE(imported.has_value());
    // One imported player gets an injury, another a place in a lineup template.
    ASSERT_TRUE(run(m_db->database(),
                    QStringLiteral("INSERT INTO availability (player_id, type, from_date) "
                                   "VALUES (?, 'injury', '2026-11-01')"),
                    {qlonglong((*imported)[0].id.value)}));
    ASSERT_TRUE(run(m_db->database(),
                    QStringLiteral("INSERT INTO lineup (template_name, formation, tactic_json) "
                                   "VALUES ('T', '4-4-2', ?)"),
                    {QStringLiteral("{\"version\":1,\"slots\":[],\"bench\":[%1]}")
                         .arg((*imported)[1].id.value)}));
    const auto undone = m_service->undo(summary->importId);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->deleted, 16);
    EXPECT_EQ(undone->archived, 2);
    EXPECT_EQ(count("SELECT count(*) FROM player WHERE archived_at IS NULL"), 1);
    EXPECT_TRUE(m_players->get(existing).has_value());
    EXPECT_TRUE(m_log->get(summary->importId)->report.undoneAt.has_value());
    expectError(m_service->undo(summary->importId), ErrorCode::Conflict, "import_undone");
}

TEST_F(ImportServiceTest, UndoCountsUpdatedPlayersAsNotReverted) {
    addPlayer("Łukasz", "Żółkiewski", 9);
    const auto summary =
        importFile(fixture("utf8-bom.csv"), {ExistingPolicy::Update, InvalidRows::Skip});
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->updated, 1);
    EXPECT_EQ(summary->created, 1);
    const auto undone = m_service->undo(summary->importId);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->notReverted, 1);
    EXPECT_EQ(undone->deleted, 1);
    const auto stored = players();
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_EQ(stored[0].birthYear, 2001); // the update stays
}

TEST_F(ImportServiceTest, LockedDatabaseWritesNothing) {
    const ImportSource source = read(fixture("utf8-bom.csv"));
    const SquadMapping mapping = suggested(source);
    auto rows = validate(source, mapping);
    {
        RawConnection other(databasePath());
        ASSERT_TRUE(run(other.database(), QStringLiteral("BEGIN EXCLUSIVE")));
        const auto summary = m_service->commitSquad(kTeam, source, mapping, rows, {});
        expectError(summary, ErrorCode::DatabaseLocked, "");
        ASSERT_TRUE(run(other.database(), QStringLiteral("ROLLBACK")));
    }
    EXPECT_EQ(count("SELECT count(*) FROM player"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM import_log"), 0);
    // Free again: the same rows go in.
    EXPECT_TRUE(m_service->commitSquad(kTeam, source, mapping, rows, {}).has_value());
}

// Packet P16, "Done when": the templates, filled in and imported, round-trip.
TEST_F(ImportServiceTest, CsvTemplateRoundTrips) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("kadra.csv"));
    QByteArray content = readFile(templateFile("squad.csv"));
    ASSERT_FALSE(content.isEmpty());
    content += "Łukasz;Żółkiewski;9;2001;N;ŚPO, PS;L\r\nJózef;Ćwiękała;1;1998;BR;;P\r\n";
    ASSERT_TRUE(writeFile(path, content));
    const ImportSource source = read(path);
    const SquadMapping mapping = suggested(source);
    EXPECT_TRUE(checkMapping(mapping, source.table).empty());
    EXPECT_TRUE(
        std::ranges::all_of(mapping.columns, [](const auto& field) { return field.has_value(); }));
    auto rows = validate(source, mapping);
    ASSERT_TRUE(m_service->commitSquad(kTeam, source, mapping, rows, {}).has_value());
    const auto stored = players();
    ASSERT_EQ(stored.size(), 2U);
    EXPECT_EQ(stored[0].birthYear, 2001);
    EXPECT_EQ(stored[0].secondaryPositions, (std::vector<Position>{Position::Am, Position::Rw}));
    EXPECT_EQ(stored[1].foot, PreferredFoot::Right);
}

TEST_F(ImportServiceTest, XlsxTemplateRoundTrips) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("kadra.xlsx"));
    {
        QXlsx::Document document(templateFile("squad.xlsx"));
        ASSERT_TRUE(document.load());
        EXPECT_EQ(document.read(1, 1).toString(), QStringLiteral("Imię"));
        document.write(2, 1, QStringLiteral("Łukasz"));
        document.write(2, 2, QStringLiteral("Żółkiewski"));
        document.write(2, 3, 9);
        document.write(2, 4, 2001);
        document.write(2, 5, QStringLiteral("N"));
        document.write(2, 7, QStringLiteral("L"));
        ASSERT_TRUE(document.saveAs(path));
    }
    const ImportSource source = read(path);
    EXPECT_EQ(source.kind, ImportFileKind::Xlsx);
    const SquadMapping mapping = suggested(source);
    EXPECT_TRUE(checkMapping(mapping, source.table).empty());
    auto rows = validate(source, mapping);
    ASSERT_TRUE(m_service->commitSquad(kTeam, source, mapping, rows, {}).has_value());
    const auto stored = players();
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_EQ(stored[0].shirtNumber, 9);
    EXPECT_EQ(stored[0].birthYear, 2001);
    EXPECT_EQ(stored[0].foot, PreferredFoot::Left);
}

TEST_F(ImportServiceTest, ValidationReportsProgress) {
    const ImportSource source = read(fixture("polish-excel-cp1250.csv"));
    auto future = m_service->validateSquad(kTeam, source, suggested(source));
    ASSERT_TRUE(future.has_value());
    future->waitForFinished();
    EXPECT_EQ(future->progressMaximum(), 20);
    EXPECT_EQ(future->progressValue(), 20);
}

bool hasIssue(const SquadRow& row, RowIssueKind kind) {
    return std::ranges::any_of(row.issues,
                               [kind](const RowIssue& issue) { return issue.kind == kind; });
}

// P16 review follow-up 1: a player added between the check and the commit changes the plan; the
// commit writes nothing (no backup either) and marks the row, and goes through once the user has
// seen the new report.
TEST_F(ImportServiceTest, SquadChangedAfterTheCheckRefusesTheCommit) {
    const auto source = read(fixture("utf8-bom.csv"));
    const auto mapping = suggested(source);
    auto rows = validate(source, mapping);
    const auto id = addPlayer("Łukasz", "Żółkiewski", std::nullopt);
    for (const ExistingPolicy policy : {ExistingPolicy::Skip, ExistingPolicy::Update}) {
        auto attempt = rows;
        expectError(
            m_service->commitSquad(kTeam, source, mapping, attempt, {policy, InvalidRows::Skip}),
            ErrorCode::Conflict, "import_matches_changed");
        EXPECT_EQ(players().size(), 1U);
        EXPECT_EQ(count("SELECT count(*) FROM import_log"), 0);
        EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());
        EXPECT_TRUE(hasIssue(attempt.front(), RowIssueKind::MatchChanged));
        EXPECT_EQ(attempt.front().existing, id);
        EXPECT_FALSE(hasIssue(attempt.back(), RowIssueKind::MatchChanged));
    }
    // The wizard's own rows: refused once and marked; the report the user then sees is the plan,
    // and the next commit writes it.
    expectError(m_service->commitSquad(kTeam, source, mapping, rows,
                                       {ExistingPolicy::Update, InvalidRows::Skip}),
                ErrorCode::Conflict, "import_matches_changed");
    EXPECT_TRUE(hasIssue(rows.front(), RowIssueKind::MatchChanged));
    const auto summary = m_service->commitSquad(kTeam, source, mapping, rows,
                                                {ExistingPolicy::Update, InvalidRows::Skip});
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->created, 1);
    EXPECT_EQ(summary->updated, 1);
    EXPECT_FALSE(hasIssue(rows.front(), RowIssueKind::MatchChanged));
    EXPECT_EQ(m_players->get(id)->birthYear, 2001);
}

// P16 review follow-up 2: with "Skip existing" the number is not changed, so the row is skipped
// and "Abort on errors" does not block the import; with "Update existing" it is an error.
TEST_F(ImportServiceTest, NumberConflictBlocksOnlyAnUpdate) {
    addPlayer("Jan", "Kowalski", 7);
    addPlayer("Adam", "Nowak", 9);
    const auto source = read(fixture("number-conflict.csv"));
    const auto mapping = suggested(source);
    auto rows = validate(source, mapping);
    ASSERT_TRUE(hasIssue(rows.front(), RowIssueKind::NumberConflict));
    expectError(m_service->commitSquad(kTeam, source, mapping, rows,
                                       {ExistingPolicy::Update, InvalidRows::Abort}),
                ErrorCode::InvalidInput, "import_has_errors");
    EXPECT_EQ(players().size(), 2U);
    EXPECT_EQ(count("SELECT count(*) FROM import_log"), 0);
    const auto skipped = m_service->commitSquad(kTeam, source, mapping, rows,
                                                {ExistingPolicy::Skip, InvalidRows::Abort});
    ASSERT_TRUE(skipped.has_value()) << skipped.error().detail;
    EXPECT_EQ(skipped->created, 1);
    EXPECT_EQ(skipped->skippedExisting, 1);
    EXPECT_EQ(skipped->invalid, 0);
    EXPECT_EQ(m_players->list(kTeam, data::ArchivedPlayers::Exclude)->front().shirtNumber, 7);
}

// The number of another player taken after the check: the commit is refused before any abort
// decision, and the row shows the conflict.
TEST_F(ImportServiceTest, NumberTakenAfterValidationIsReportedBeforeAbort) {
    addPlayer("Łukasz", "Żółkiewski", 7);
    const auto source = read(fixture("utf8-bom.csv"));
    const auto mapping = suggested(source);
    auto rows = validate(source, mapping);
    ASSERT_EQ(rows.front().status(), RowStatus::Ok);
    addPlayer("Adam", "Nowak", 9);
    expectError(m_service->commitSquad(kTeam, source, mapping, rows,
                                       {ExistingPolicy::Update, InvalidRows::Abort}),
                ErrorCode::Conflict, "import_matches_changed");
    EXPECT_EQ(statusFor(rows.front(), ExistingPolicy::Update), RowStatus::Error);
    EXPECT_TRUE(hasIssue(rows.front(), RowIssueKind::NumberConflict));
    EXPECT_TRUE(hasIssue(rows.front(), RowIssueKind::MatchChanged));
    EXPECT_EQ(players().size(), 2U);
    EXPECT_EQ(count("SELECT count(*) FROM import_log"), 0);
}

// P16 review follow-up 3: a refused import makes no backup; an accepted one makes exactly one.
TEST_F(ImportServiceTest, RefusedImportsMakeNoBackup) {
    const auto errors = read(fixture("unknown-positions.csv"));
    const auto errorMapping = suggested(errors);
    auto errorRows = validate(errors, errorMapping);
    expectError(m_service->commitSquad(kTeam, errors, errorMapping, errorRows,
                                       {ExistingPolicy::Skip, InvalidRows::Abort}),
                ErrorCode::InvalidInput, "import_has_errors");
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());

    addPlayer("Łukasz", "Żółkiewski", 9);
    addPlayer("Józef", "Ćwiękała", 1);
    const auto known = read(fixture("utf8-bom.csv"));
    const auto knownMapping = suggested(known);
    auto knownRows = validate(known, knownMapping);
    expectError(m_service->commitSquad(kTeam, known, knownMapping, knownRows, {}),
                ErrorCode::InvalidInput, "import_nothing");
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());

    m_session.clear();
    expectError(m_service->commitSquad(kTeam, errors, errorMapping, errorRows, {}),
                ErrorCode::PermissionDenied, "");
    EXPECT_TRUE(filesIn(backupDirectory()).isEmpty());

    logInAs(Role::Coach);
    auto fresh = validate(errors, errorMapping); // checked against the squad as it is now
    ASSERT_TRUE(m_service->commitSquad(kTeam, errors, errorMapping, fresh, {}).has_value());
    EXPECT_EQ(filesIn(backupDirectory()).size(), 1);
}

// P16 review follow-up 5: the line of an open quote travels beside the error, whose field stays a
// field name.
TEST(ImportFileTest, OpenQuoteKeepsItsLineOutsideTheField) {
    const auto source = readImportFile(fixture("unterminated-quote.csv"));
    ASSERT_TRUE(source.has_value()) << source.error().detail;
    expectError(source->tableStatus, ErrorCode::InvalidInput, "import_csv_quote");
    EXPECT_EQ(source->errorLine, 2);
    EXPECT_TRUE(source->table.rows.empty());
}

} // namespace
