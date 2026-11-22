#include "services/import/ImportService.hpp"

#include "../data/TestDatabase.hpp"
#include "FakeClock.hpp"
#include "core/domain/StaffUser.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/repositories/ImportLogRepository.hpp"
#include "data/repositories/MatchLiveRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/AuthService.hpp"
#include "services/LoadService.hpp"
#include "services/Session.hpp"

#include <QFile>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

// Training and match imports (packet P18) on a file database, so commits make real backups.

using namespace fdss;
using namespace fdss::core;
using namespace fdss::services;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

constexpr TeamId kTeam{1};

QString fixture(const char* name) {
    return QStringLiteral(FDSS_IMPORT_FIXTURES_DIR "/%1").arg(QLatin1StringView(name));
}

QString templateFile(const char* name) {
    return QStringLiteral(FDSS_IMPORT_TEMPLATES_DIR "/%1").arg(QLatin1StringView(name));
}

Date october(unsigned day) { return Date{year{2026}, October, std::chrono::day{day}}; }

template <class T>
void expectError(const Result<T>& result, ErrorCode code, std::string_view field) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, code) << result.error().detail;
    EXPECT_EQ(result.error().field, field) << result.error().detail;
}

class ActivityImportServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_dir.isValid());
        data::OpenOptions options;
        options.busyTimeoutMs = 100;
        options.backupDirectory = m_dir.filePath(QStringLiteral("backups"));
        auto opened =
            data::DatabaseManager::open(m_dir.filePath(QStringLiteral("club.sqlite")), options);
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
        m_training = std::make_unique<data::TrainingRepository>(*m_db);
        m_matches = std::make_unique<data::MatchRepository>(*m_db);
        m_matchLive = std::make_unique<data::MatchLiveRepository>(*m_db);
        m_metrics = std::make_unique<data::MetricRepository>(*m_db);
        m_opponents = std::make_unique<data::OpponentRepository>(*m_db);
        m_service = std::make_unique<ImportService>(
            *m_db,
            ImportRepositories{*m_log, *m_players, *m_training, *m_matches, *m_matchLive,
                               *m_metrics, *m_opponents},
            m_session, m_clock);
        logIn();
        m_kowalski = addPlayer("Jan", "Kowalski", 9);
        m_piotr = addPlayer("Piotr", "Nowak", 4);
        m_adam = addPlayer("Adam", "Nowak", 5);
        m_wisniewski = addPlayer("Łukasz", "Wiśniewski", 7);
    }

    void logIn() {
        StaffUserDraft draft;
        draft.name = "Trener";
        draft.role = Role::Coach;
        const auto hash = m_auth->hashPin("1234");
        ASSERT_TRUE(hash.has_value());
        const auto id = m_staff->create(draft, *hash);
        ASSERT_TRUE(id.has_value());
        const auto login = m_auth->login(*id, "1234");
        ASSERT_TRUE(login.has_value());
    }

    PlayerId addPlayer(const char* first, const char* last, std::optional<int> number) {
        PlayerDraft draft;
        draft.firstName = first;
        draft.lastName = last;
        draft.shirtNumber = number;
        const auto id = m_players->create(kTeam, draft);
        EXPECT_TRUE(id.has_value());
        return id.value_or(PlayerId{});
    }

    MetricDefinitionId addSprintMetric() {
        MetricDefinitionDraft draft;
        draft.namePl = "Sprint 30 m";
        draft.valueType = MetricValueType::Decimal;
        draft.unit = "s";
        draft.min = 2.0;
        draft.max = 10.0;
        draft.aggregation = MetricAggregation::Min;
        draft.scope = MetricScope::Training;
        draft.higherIsBetter = false;
        const auto id = m_metrics->createDefinition(draft, "sprint_30_m");
        EXPECT_TRUE(id.has_value());
        return id.value_or(MetricDefinitionId{});
    }

    ImportSource read(const QString& path, ImportKind kind) {
        auto future = m_service->read(path, std::nullopt, kind);
        EXPECT_TRUE(future.has_value());
        if (!future) {
            return {};
        }
        auto source = future->result();
        EXPECT_TRUE(source.has_value()) << source.error().detail;
        return source.value_or(ImportSource{});
    }

    struct Training {
        ImportSource source;
        ActivityMapping mapping;
        TrainingImport import;
        TrainingPlan plan;
    };

    Training checkTraining(const QString& path, const ActivityOptions& options) {
        Training result;
        result.source = read(path, ImportKind::Training);
        auto suggestion = m_service->suggestActivityMapping(ImportKind::Training, result.source);
        EXPECT_TRUE(suggestion.has_value());
        result.mapping = suggestion ? suggestion->mapping : ActivityMapping{};
        auto future = m_service->validateTraining(kTeam, result.source, result.mapping);
        EXPECT_TRUE(future.has_value());
        result.import = future ? future->result() : TrainingImport{};
        auto plan = m_service->planTraining(kTeam, result.import, result.mapping, options);
        EXPECT_TRUE(plan.has_value());
        result.plan = plan.value_or(TrainingPlan{});
        return result;
    }

    Result<ActivityImportSummary> importTraining(const QString& path,
                                                 const ActivityOptions& options = {}) {
        const Training checked = checkTraining(path, options);
        return m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                         checked.plan, options);
    }

    struct Matches {
        ImportSource source;
        ActivityMapping mapping;
        MatchImport import;
        MatchPlan plan;
    };

    Matches checkMatches(const QString& path, const ActivityOptions& options) {
        Matches result;
        result.source = read(path, ImportKind::Matches);
        auto suggestion = m_service->suggestActivityMapping(ImportKind::Matches, result.source);
        EXPECT_TRUE(suggestion.has_value());
        result.mapping = suggestion ? suggestion->mapping : ActivityMapping{};
        auto future = m_service->validateMatches(kTeam, result.source, result.mapping);
        EXPECT_TRUE(future.has_value());
        result.import = future ? future->result() : MatchImport{};
        auto plan = m_service->planMatches(kTeam, result.import, result.mapping, options);
        EXPECT_TRUE(plan.has_value());
        result.plan = plan.value_or(MatchPlan{});
        return result;
    }

    Result<ActivityImportSummary> importMatches(const QString& path,
                                                const ActivityOptions& options = {}) {
        const Matches checked = checkMatches(path, options);
        return m_service->commitMatches(kTeam, checked.source, checked.mapping, checked.import,
                                        checked.plan, options);
    }

    [[nodiscard]] int count(const char* sql) const {
        return scalar(m_db->database(), QString::fromUtf8(sql)).toInt();
    }

    // Every row of the tables an import writes, for "restores the previous state".
    [[nodiscard]] QString dump() const {
        QStringList out;
        for (const char* table :
             {"player", "opponent", "match", "appearance", "match_event", "match_note", "lineup",
              "training_session", "attendance", "metric_definition", "metric_value"}) {
            QSqlQuery query(m_db->database());
            EXPECT_TRUE(query.exec(
                QStringLiteral("SELECT * FROM %1 ORDER BY id").arg(QLatin1StringView(table))));
            while (query.next()) {
                QStringList cells{QString::fromLatin1(table)};
                for (int column = 0; column < query.record().count(); ++column) {
                    cells << query.value(column).toString();
                }
                out << cells.join(QLatin1Char('|'));
            }
        }
        return out.join(QLatin1Char('\n'));
    }

    QTemporaryDir m_dir;
    std::unique_ptr<data::DatabaseManager> m_db;
    FakeClock m_clock; // 2026-11-01
    Session m_session;
    std::unique_ptr<data::StaffRepository> m_staff;
    std::unique_ptr<AuthService> m_auth;
    std::unique_ptr<data::ImportLogRepository> m_log;
    std::unique_ptr<data::PlayerRepository> m_players;
    std::unique_ptr<data::TrainingRepository> m_training;
    std::unique_ptr<data::MatchRepository> m_matches;
    std::unique_ptr<data::MatchLiveRepository> m_matchLive;
    std::unique_ptr<data::MetricRepository> m_metrics;
    std::unique_ptr<data::OpponentRepository> m_opponents;
    std::unique_ptr<ImportService> m_service;
    PlayerId m_kowalski;
    PlayerId m_piotr;
    PlayerId m_adam;
    PlayerId m_wisniewski;
};

// --- Training ---

TEST_F(ActivityImportServiceTest, TrainingFromPolishExcelCsv) {
    const MetricDefinitionId sprint = addSprintMetric();
    const Training checked = checkTraining(fixture("training-cp1250.csv"), {});
    EXPECT_EQ(checked.source.encoding, TextEncoding::Cp1250);
    EXPECT_EQ(checked.source.delimiter, ';');
    // "Sprint 30 m" is the metric of the club.
    EXPECT_EQ(checked.mapping.columns.back().metric, sprint);

    auto summary = m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                             checked.plan, {});
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->sessionsCreated, 12);
    EXPECT_EQ(summary->created, 36);
    EXPECT_EQ(summary->skipped, 2); // the unknown and the ambiguous player
    EXPECT_EQ(summary->invalid, 1); // RPE 11
    EXPECT_EQ(summary->metricValues, 4);
    EXPECT_EQ(count("SELECT COUNT(*) FROM training_session WHERE import_id IS NOT NULL"), 12);
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance WHERE import_id IS NOT NULL"), 36);
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance WHERE status = 'injured' AND rpe IS NULL"), 1);
    // Decimal comma: 4,35 s.
    EXPECT_DOUBLE_EQ(
        scalar(m_db->database(), QStringLiteral("SELECT MIN(value) FROM metric_value")).toDouble(),
        4.35);
    // The durations come from the longest minutes of each session.
    EXPECT_EQ(count("SELECT duration_min FROM training_session WHERE date = '2026-10-07'"), 75);
    EXPECT_EQ(count("SELECT start_time = '18:00' FROM training_session LIMIT 1"), 1);
}

TEST_F(ActivityImportServiceTest, LoadAfterTheImportEqualsAHandCalculation) {
    ASSERT_TRUE(importTraining(fixture("training-cp1250.csv")).has_value());
    LoadService load(*m_players, *m_training, *m_matches, m_session, m_clock);
    auto future = load.playerLoad(m_kowalski);
    ASSERT_TRUE(future.has_value());
    const PlayerLoadDetail detail = future->result();
    const auto dayLoad = [&](unsigned day) {
        const auto it = std::ranges::find(detail.load.days, october(day), &LoadDay::date);
        return it == detail.load.days.end() ? -1 : it->sessionLoad;
    };
    // RPE × minutes of Jan Kowalski in the file.
    EXPECT_EQ(dayLoad(5), 5 * 90);
    EXPECT_EQ(dayLoad(7), 7 * 75);
    EXPECT_EQ(dayLoad(9), 4 * 60);
    EXPECT_EQ(dayLoad(26), 6 * 90);
    EXPECT_EQ(dayLoad(28), 8 * 80);
    EXPECT_EQ(dayLoad(30), 3 * 45);
    EXPECT_EQ(dayLoad(6), 0);
    // The last 7 days through 1 November: 540 + 640 + 135.
    EXPECT_EQ(detail.load.assessment.indicators.weekLoad, 1315);
}

TEST_F(ActivityImportServiceTest, SameFileTwiceChangesNothing) {
    ASSERT_TRUE(importTraining(fixture("training-cp1250.csv")).has_value());
    const QString before = dump();
    const ImportSource source = read(fixture("training-cp1250.csv"), ImportKind::Training);
    auto previous = m_service->previousImports(source, ImportKind::Training);
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(previous->size(), 1U);
    const Training checked = checkTraining(fixture("training-cp1250.csv"), {});
    EXPECT_EQ(std::ranges::count(checked.plan.rows, ActivityAction::Unchanged,
                                 &PlannedTrainingRow::action),
              36);
    expectError(m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                          checked.plan, {}),
                ErrorCode::InvalidInput, "import_nothing");
    EXPECT_EQ(dump(), before);
}

TEST_F(ActivityImportServiceTest, UndoRestoresThePreviousState) {
    addSprintMetric();
    const QString before = dump();
    ActivityOptions create;
    create.unknownPlayers = UnknownPlayerPolicy::Create;
    create.defaultPosition = Position::St;
    auto summary = importTraining(fixture("training-cp1250.csv"), create);
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->playersCreated, 1);
    EXPECT_EQ(count("SELECT COUNT(*) FROM player WHERE last_name = 'Zieliński' AND import_id "
                    "IS NOT NULL"),
              1);
    auto preview = m_service->undoPreview(summary->importId);
    ASSERT_TRUE(preview.has_value());
    EXPECT_FALSE(preview->any());
    auto undone = m_service->undo(summary->importId);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->sessionsDeleted, 12);
    EXPECT_EQ(undone->deleted, 1);
    EXPECT_EQ(dump(), before);
    expectError(m_service->undo(summary->importId), ErrorCode::Conflict, "import_undone");
}

TEST_F(ActivityImportServiceTest, UpdateOverwritesAndUndoPutsTheOldValuesBack) {
    TrainingSessionDraft session;
    session.date = october(5);
    session.startTime = TimeOfDay{18 * 60};
    const auto id = m_training->create(kTeam, session);
    ASSERT_TRUE(id.has_value());
    AttendanceDraft stored;
    stored.playerId = m_kowalski;
    stored.rpe = 3;
    ASSERT_TRUE(m_training->saveAttendance(*id, std::vector{stored}).has_value());
    const QString before = dump();

    // Update off: the stored row is kept, with a warning.
    const Training kept = checkTraining(fixture("training-cp1250.csv"), {});
    EXPECT_EQ(kept.plan.sessions[0].existing, *id);
    EXPECT_EQ(kept.plan.rows[0].action, ActivityAction::KeepExisting);

    ActivityOptions update;
    update.updateExisting = true;
    auto summary = importTraining(fixture("training-cp1250.csv"), update);
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->updated, 1);
    EXPECT_EQ(summary->sessionsCreated, 11);
    EXPECT_EQ(count("SELECT rpe FROM attendance WHERE player_id = 1 AND session_id = 1"), 5);
    auto undone = m_service->undo(summary->importId);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->rowsRestored, 1);
    EXPECT_EQ(dump(), before);
}

TEST_F(ActivityImportServiceTest, UndoAsksAboutValuesAddedAfterTheImport) {
    const MetricDefinitionId sprint = addSprintMetric();
    auto summary = importTraining(fixture("training-cp1250.csv"));
    ASSERT_TRUE(summary.has_value());
    const auto sessions = m_training->listByImport(summary->importId);
    ASSERT_TRUE(sessions.has_value());
    // A value entered by hand in an imported session, and an RPE corrected on the sheet.
    const std::vector values = {MetricValueDraft{m_piotr, 5.1}};
    ASSERT_TRUE(
        m_metrics->saveValues(sprint, sessions->front().id, sessions->front().date, values));
    auto attendance = m_training->listAttendance(sessions->front().id);
    ASSERT_TRUE(attendance.has_value());
    std::vector<AttendanceDraft> drafts;
    for (const Attendance& row : *attendance) {
        AttendanceDraft draft = row;
        if (draft.playerId == m_kowalski) {
            draft.rpe = 9;
        }
        drafts.push_back(draft);
    }
    ASSERT_TRUE(m_training->saveAttendance(sessions->front().id, drafts).has_value());
    // Saving the sheet keeps the import_id (decision 11).
    EXPECT_EQ(count("SELECT COUNT(*) FROM attendance WHERE import_id IS NOT NULL"), 36);

    auto preview = m_service->undoPreview(summary->importId);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->values, 1);
    EXPECT_EQ(preview->entries, 1);
    ASSERT_TRUE(m_service->undo(summary->importId).has_value());
    EXPECT_EQ(count("SELECT COUNT(*) FROM metric_value"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM training_session"), 0);
}

TEST_F(ActivityImportServiceTest, ThePlanMustBeTheOneTheUserSaw) {
    const Training checked = checkTraining(fixture("training-cp1250.csv"), {});
    TrainingSessionDraft session;
    session.date = october(5);
    session.startTime = TimeOfDay{18 * 60};
    ASSERT_TRUE(m_training->create(kTeam, session).has_value());
    expectError(m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                          checked.plan, {}),
                ErrorCode::Conflict, "import_matches_changed");
    EXPECT_EQ(count("SELECT COUNT(*) FROM import_log"), 0);
}

TEST_F(ActivityImportServiceTest, InvalidRowsCanAbort) {
    ActivityOptions abort;
    abort.skipInvalid = false;
    expectError(importTraining(fixture("training-cp1250.csv"), abort), ErrorCode::InvalidInput,
                "import_has_errors");
    EXPECT_EQ(count("SELECT COUNT(*) FROM training_session"), 0);
}

// P19 decision 7: without a default position the new players have none ("—").
TEST_F(ActivityImportServiceTest, NewPlayersWithoutADefaultPositionHaveNone) {
    const int before = count("SELECT COUNT(*) FROM player");
    const int withoutBefore = count("SELECT COUNT(*) FROM player WHERE primary_position IS NULL");
    ActivityOptions create;
    create.unknownPlayers = UnknownPlayerPolicy::Create;

    const auto imported = importTraining(fixture("training-cp1250.csv"), create);

    ASSERT_TRUE(imported.has_value()) << imported.error().detail;
    const int created = count("SELECT COUNT(*) FROM player") - before;
    EXPECT_GT(created, 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM player WHERE primary_position IS NULL") - withoutBefore,
              created);
}

TEST_F(ActivityImportServiceTest, TrainingXlsxWithExcelDates) {
    auto summary = importTraining(fixture("training.xlsx"));
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->sessionsCreated, 2);
    // A date cell and a plain serial number (46309).
    EXPECT_EQ(count("SELECT COUNT(*) FROM training_session WHERE date IN ('2026-10-12', "
                    "'2026-10-14') AND start_time = '17:30'"),
              2);
}

TEST_F(ActivityImportServiceTest, WideFormatIsRefused) {
    auto future = m_service->read(fixture("training-wide.csv"), std::nullopt, ImportKind::Training);
    ASSERT_TRUE(future.has_value());
    const auto source = future->result();
    ASSERT_TRUE(source.has_value());
    expectError(source->tableStatus, ErrorCode::UnsupportedFormat, "import_wide_format");
}

TEST_F(ActivityImportServiceTest, TemplatesHaveOnlyTheExampleRow) {
    for (const char* name : {"training.csv", "training.xlsx"}) {
        const Training checked = checkTraining(templateFile(name), {});
        ASSERT_EQ(checked.import.rows.size(), 1U) << name;
        EXPECT_TRUE(checked.import.rows[0].templateExample) << name;
    }
    for (const char* name : {"matches.csv", "matches.xlsx"}) {
        const Matches checked = checkMatches(templateFile(name), {});
        ASSERT_EQ(checked.import.rows.size(), 1U) << name;
        EXPECT_TRUE(checked.import.rows[0].templateExample) << name;
    }
}

// --- Matches ---

TEST_F(ActivityImportServiceTest, MatchesFromPolishExcelCsvAndUndo) {
    const QString before = dump();
    auto summary = importMatches(fixture("matches-cp1250.csv"));
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->matchesCreated, 3);
    EXPECT_EQ(summary->opponentsCreated, 2);
    EXPECT_EQ(summary->created, 7);
    EXPECT_EQ(summary->skipped, 2); // unknown and ambiguous
    EXPECT_EQ(summary->invalid, 1); // 140 minutes
    EXPECT_EQ(summary->notImported.goals, 4);
    EXPECT_EQ(summary->notImported.assists, 3);
    EXPECT_EQ(summary->notImported.cards, 2);
    EXPECT_EQ(summary->notImported.yellowCards, 2);
    EXPECT_EQ(count("SELECT COUNT(*) FROM match WHERE status = 'finished' AND import_id IS NOT "
                    "NULL"),
              3);
    EXPECT_EQ(count("SELECT COUNT(*) FROM match_event"), 0);
    EXPECT_EQ(count("SELECT goals_for * 10 + goals_against FROM match WHERE date = '2026-10-24'"),
              32);
    // A substitute for 20 minutes: 70 -> 90.
    EXPECT_EQ(count("SELECT minute_on FROM appearance WHERE player_id = 4 AND started = 0"), 70);
    EXPECT_EQ(count("SELECT minute_off FROM appearance WHERE player_id = 2 AND minute_on = 0 "
                    "AND match_id = (SELECT id FROM match WHERE date = '2026-10-10')"),
              70);

    // The match load counts: RPE × minutes of a finished match.
    LoadService load(*m_players, *m_training, *m_matches, m_session, m_clock);
    auto future = load.playerLoad(m_kowalski);
    ASSERT_TRUE(future.has_value());
    const PlayerLoadDetail detail = future->result();
    const auto day = std::ranges::find(detail.load.days, october(10), &LoadDay::date);
    ASSERT_NE(day, detail.load.days.end());
    EXPECT_EQ(day->matchLoad, 8 * 90);

    auto undone = m_service->undo(summary->importId);
    ASSERT_TRUE(undone.has_value()) << undone.error().detail;
    EXPECT_EQ(undone->matchesDeleted, 3);
    EXPECT_EQ(undone->opponentsDeleted, 2);
    EXPECT_EQ(dump(), before);
}

TEST_F(ActivityImportServiceTest, MatchNotesAddedLaterAreCountedBeforeTheUndo) {
    auto summary = importMatches(fixture("matches-cp1250.csv"));
    ASSERT_TRUE(summary.has_value());
    ASSERT_TRUE(run(m_db->database(),
                    QStringLiteral("INSERT INTO match_note (match_id, minute, text) "
                                   "SELECT id, 10, 'Dobry pressing' FROM match LIMIT 1")));
    ASSERT_TRUE(run(m_db->database(),
                    QStringLiteral("UPDATE appearance SET rating = 9 WHERE player_id = 1 AND "
                                   "match_id = (SELECT id FROM match WHERE date = '2026-10-17')")));
    auto preview = m_service->undoPreview(summary->importId);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->notes, 1);
    EXPECT_EQ(preview->entries, 1);
    ASSERT_TRUE(m_service->undo(summary->importId).has_value());
    EXPECT_EQ(count("SELECT COUNT(*) FROM match_note"), 0);
}

TEST_F(ActivityImportServiceTest, AFinishedMatchGetsOnlyTheMissingAppearances) {
    const auto opponent = m_opponents->create(OpponentDraft{"Wisła Kraków", std::nullopt});
    ASSERT_TRUE(opponent.has_value());
    MatchDraft draft;
    draft.opponentId = *opponent;
    draft.date = october(31);
    const auto match = m_matches->create(kTeam, draft);
    ASSERT_TRUE(match.has_value());
    ASSERT_TRUE(m_matches->setResult(*match, 1, 1, MatchStatus::Finished).has_value());
    AppearanceDraft stored;
    stored.playerId = m_kowalski;
    stored.started = true;
    stored.minuteOn = 0;
    stored.minuteOff = 90;
    stored.rating = 6;
    ASSERT_TRUE(m_matches->replaceAppearances(*match, std::vector{stored}).has_value());

    auto summary = importMatches(fixture("matches.xlsx"));
    ASSERT_TRUE(summary.has_value()) << summary.error().detail;
    EXPECT_EQ(summary->matchesCreated, 0);
    EXPECT_EQ(summary->unchanged, 1); // Jan Kowalski as stored
    EXPECT_EQ(summary->created, 1);   // Piotr Nowak, 25 minutes from the bench
    EXPECT_EQ(count("SELECT minute_on FROM appearance WHERE player_id = 2"), 65);
}

TEST_F(ActivityImportServiceTest, AMatchOfMatchDayIsNotTouched) {
    const auto opponent = m_opponents->create(OpponentDraft{"Wisła Kraków", std::nullopt});
    ASSERT_TRUE(opponent.has_value());
    MatchDraft draft;
    draft.opponentId = *opponent;
    draft.date = october(31);
    const auto match = m_matches->create(kTeam, draft);
    ASSERT_TRUE(match.has_value());
    ASSERT_TRUE(m_matches->setStatus(*match, MatchStatus::Live).has_value());
    const Matches checked = checkMatches(fixture("matches.xlsx"), {});
    ASSERT_FALSE(checked.plan.rows.empty());
    EXPECT_EQ(checked.plan.rows[0].action, ActivityAction::Invalid);
    expectError(m_service->commitMatches(kTeam, checked.source, checked.mapping, checked.import,
                                         checked.plan, {}),
                ErrorCode::InvalidInput, "import_nothing");
}

// --- Review of P18 ---

TEST_F(ActivityImportServiceTest, SwappedShirtNumbersAfterTheCheckWriteNothing) {
    const QString file = m_dir.filePath(QStringLiteral("numbers.csv"));
    {
        QFile out(file);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        out.write("Data;Nr;RPE;Minuty\n05.10.2026;9;6;90\n05.10.2026;4;3;90\n");
    }
    const Training checked = checkTraining(file, {});
    ASSERT_EQ(checked.plan.rows[0].player.existing, m_kowalski);
    ASSERT_TRUE(run(m_db->database(),
                    QStringLiteral("UPDATE player SET shirt_number = NULL WHERE id = 1")));
    ASSERT_TRUE(
        run(m_db->database(), QStringLiteral("UPDATE player SET shirt_number = 9 WHERE id = 2")));
    ASSERT_TRUE(
        run(m_db->database(), QStringLiteral("UPDATE player SET shirt_number = 4 WHERE id = 1")));
    expectError(m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                          checked.plan, {}),
                ErrorCode::Conflict, "import_matches_changed");
    EXPECT_EQ(count("SELECT count(*) FROM attendance"), 0);
    // The report of now marks the rows and writes the swapped players.
    auto plan = m_service->planTraining(kTeam, checked.import, checked.mapping, {});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->rows[0].player.existing, m_piotr);
    EXPECT_TRUE(std::ranges::any_of(plan->rows[0].issues, [](const ActivityIssue& issue) {
        return issue.kind == ActivityIssueKind::PlanChanged;
    }));
    ASSERT_TRUE(m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                          *plan, {}));
    EXPECT_EQ(count("SELECT rpe FROM attendance WHERE player_id = 2"), 6);
}

TEST_F(ActivityImportServiceTest, AMetricChangedAfterTheCheckWritesNothing) {
    addSprintMetric();
    const Training checked = checkTraining(fixture("training-cp1250.csv"), {});
    ASSERT_TRUE(run(m_db->database(), QStringLiteral("UPDATE metric_definition SET scope = "
                                                     "'match'")));
    expectError(m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                          checked.plan, {}),
                ErrorCode::Conflict, "import_matches_changed");
    EXPECT_EQ(count("SELECT count(*) FROM training_session"), 0);
    // The plan of now makes the rows with a sprint value errors; the rest goes in.
    auto plan = m_service->planTraining(kTeam, checked.import, checked.mapping, {});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(std::ranges::count_if(
                  plan->rows,
                  [](const PlannedTrainingRow& row) {
                      return std::ranges::any_of(row.issues, [](const ActivityIssue& issue) {
                          return issue.kind == ActivityIssueKind::MetricChanged;
                      });
                  }),
              4);
    ASSERT_TRUE(m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                          *plan, {}));
    EXPECT_EQ(count("SELECT count(*) FROM metric_value"), 0);
}

TEST_F(ActivityImportServiceTest, ASessionNoteAddedLaterIsNamedBeforeTheUndo) {
    auto summary = importTraining(fixture("training-cp1250.csv"));
    ASSERT_TRUE(summary.has_value());
    ASSERT_TRUE(run(m_db->database(), QStringLiteral("UPDATE training_session SET note = "
                                                     "'Boisko B' WHERE date = '2026-10-05'")));
    ASSERT_TRUE(run(m_db->database(), QStringLiteral("UPDATE training_session SET duration_min = "
                                                     "100 WHERE date = '2026-10-07'")));
    auto preview = m_service->undoPreview(summary->importId);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->notes, 1);
    EXPECT_EQ(preview->entries, 1);
}

TEST_F(ActivityImportServiceTest, ACorrectedImportedValueGoesWithTheUndo) {
    const MetricDefinitionId sprint = addSprintMetric();
    TrainingSessionDraft session;
    session.date = october(9);
    session.startTime = TimeOfDay{18 * 60};
    const auto id = m_training->create(kTeam, session);
    ASSERT_TRUE(id.has_value());
    const QString before = dump();
    auto summary = importTraining(fixture("training-cp1250.csv"));
    ASSERT_TRUE(summary.has_value());
    // The imported 4,35 in the session that existed, corrected on the entry grid.
    const std::vector values = {MetricValueDraft{m_kowalski, 4.2}};
    ASSERT_TRUE(m_metrics->saveValues(sprint, *id, october(9), values).has_value());
    EXPECT_EQ(count("SELECT count(*) FROM metric_value WHERE value = 4.2 AND import_id IS NOT "
                    "NULL"),
              1);
    auto preview = m_service->undoPreview(summary->importId);
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->values, 1);
    ASSERT_TRUE(m_service->undo(summary->importId).has_value());
    EXPECT_EQ(dump(), before);
}

// Atomicity: an SQL failure in the middle of the commit or the undo leaves the database as before.
TEST_F(ActivityImportServiceTest, AFailedWriteChangesNothing) {
    const QString before = dump();
    ASSERT_TRUE(
        run(m_db->database(),
            QStringLiteral("CREATE TEMP TRIGGER fail_attendance BEFORE INSERT ON attendance "
                           "WHEN (SELECT count(*) FROM attendance) >= 20 "
                           "BEGIN SELECT RAISE(ABORT, 'forced'); END")));
    EXPECT_FALSE(importTraining(fixture("training-cp1250.csv")).has_value());
    ASSERT_TRUE(
        run(m_db->database(),
            QStringLiteral("CREATE TEMP TRIGGER fail_appearance BEFORE INSERT ON appearance "
                           "WHEN (SELECT count(*) FROM appearance) >= 4 "
                           "BEGIN SELECT RAISE(ABORT, 'forced'); END")));
    EXPECT_FALSE(importMatches(fixture("matches-cp1250.csv")).has_value());
    EXPECT_EQ(dump(), before);
    EXPECT_EQ(count("SELECT count(*) FROM import_log"), 0);
}

TEST_F(ActivityImportServiceTest, AFailedUndoChangesNothing) {
    auto training = importTraining(fixture("training-cp1250.csv"));
    ASSERT_TRUE(training.has_value());
    auto matches = importMatches(fixture("matches-cp1250.csv"));
    ASSERT_TRUE(matches.has_value());
    const QString before = dump();
    ASSERT_TRUE(run(m_db->database(),
                    QStringLiteral("CREATE TEMP TRIGGER fail_session BEFORE DELETE ON "
                                   "training_session BEGIN SELECT RAISE(ABORT, 'forced'); END")));
    ASSERT_TRUE(run(m_db->database(),
                    QStringLiteral("CREATE TEMP TRIGGER fail_match BEFORE DELETE ON match "
                                   "BEGIN SELECT RAISE(ABORT, 'forced'); END")));
    EXPECT_FALSE(m_service->undo(training->importId).has_value());
    EXPECT_FALSE(m_service->undo(matches->importId).has_value());
    EXPECT_EQ(dump(), before);
    EXPECT_FALSE(m_log->get(training->importId)->report.undoneAt.has_value());
    EXPECT_FALSE(m_log->get(matches->importId)->report.undoneAt.has_value());
}

// SPEC section 8: the database file deleted while the app runs.
TEST_F(ActivityImportServiceTest, ADeletedDatabaseFileStopsTheImport) {
#ifdef Q_OS_WIN
    // Windows does not let anyone delete a file SQLite holds open, so the case cannot happen there.
    GTEST_SKIP() << "an open database file cannot be deleted on Windows";
#else
    auto imported = importTraining(fixture("training.xlsx"));
    ASSERT_TRUE(imported.has_value());
    const Training checked = checkTraining(fixture("training-cp1250.csv"), {});
    ASSERT_TRUE(QFile::remove(m_dir.filePath(QStringLiteral("club.sqlite"))));
    expectError(m_service->commitTraining(kTeam, checked.source, checked.mapping, checked.import,
                                          checked.plan, {}),
                ErrorCode::Io, "database_missing");
    expectError(m_service->undo(imported->importId), ErrorCode::Io, "database_missing");
    // The open connection still sees the old file: nothing was written into it.
    EXPECT_EQ(count("SELECT count(*) FROM training_session"), 2);
    EXPECT_EQ(count("SELECT count(*) FROM import_log"), 1);
#endif
}

} // namespace
