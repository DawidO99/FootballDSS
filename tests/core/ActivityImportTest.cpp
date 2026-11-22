#include "core/parsing/ActivityImport.hpp"
#include "core/parsing/ImportValues.hpp"
#include "core/parsing/MatchImport.hpp"
#include "core/parsing/PlayerMatching.hpp"
#include "core/parsing/TrainingImport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace std::chrono;

namespace {

Date ymd(int y, unsigned m, unsigned d) { return Date{year{y}, month{m}, day{d}}; }

Player player(std::int64_t id, std::string first, std::string last, std::optional<int> number) {
    Player p;
    p.id = PlayerId{id};
    p.teamId = TeamId{1};
    p.firstName = std::move(first);
    p.lastName = std::move(last);
    p.shirtNumber = number;
    return p;
}

std::vector<Player> squad() {
    return {player(1, "Jan", "Kowalski", 9), player(2, "Piotr", "Nowak", 4),
            player(3, "Adam", "Nowak", 5), player(4, "Łukasz", "Wiśniewski", std::nullopt)};
}

SourceTable table(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows) {
    SourceTable result;
    result.headerRow = 1;
    result.headers = std::move(headers);
    int number = 2;
    for (auto& cells : rows) {
        result.rows.push_back(SourceRow{number++, std::move(cells)});
    }
    return result;
}

bool hasIssue(const std::vector<ActivityIssue>& issues, ActivityIssueKind kind) {
    return std::ranges::any_of(issues,
                               [&](const ActivityIssue& issue) { return issue.kind == kind; });
}

MetricDefinition metric(std::int64_t id, std::string namePl, std::string key, MetricScope scope) {
    MetricDefinition definition;
    definition.id = MetricDefinitionId{id};
    definition.namePl = std::move(namePl);
    definition.key = std::move(key);
    definition.valueType = MetricValueType::Decimal;
    definition.min = 0.0;
    definition.max = 50.0;
    definition.scope = scope;
    return definition;
}

const std::vector<std::string> kTrainingHeaders = {
    "Data", "Godzina", "Czas trwania", "Typ", "Zawodnik", "Obecność", "RPE", "Minuty"};

} // namespace

// --- Values ---

TEST(ImportValuesTest, DatesInTheFormatsOfTheSpec) {
    EXPECT_EQ(parseImportDate("02.03.2026", false), ymd(2026, 3, 2));
    EXPECT_EQ(parseImportDate("2.3.2026", false), ymd(2026, 3, 2));
    EXPECT_EQ(parseImportDate("2026-03-02", false), ymd(2026, 3, 2));
    EXPECT_EQ(parseImportDate("02/03/2026", false), ymd(2026, 3, 2));
    EXPECT_EQ(parseImportDate(" 2026-03-02T00:00 ", false), ymd(2026, 3, 2));
    EXPECT_EQ(parseImportDate("02.03.2026 18:00", false), ymd(2026, 3, 2));
}

TEST(ImportValuesTest, BadDatesAreRefused) {
    EXPECT_EQ(parseImportDate("30.02.2026", false), std::nullopt);
    EXPECT_EQ(parseImportDate("02-03-2026", false), std::nullopt);
    EXPECT_EQ(parseImportDate("2026.03.02", false), std::nullopt);
    EXPECT_EQ(parseImportDate("wtorek", false), std::nullopt);
    EXPECT_EQ(parseImportDate("", false), std::nullopt);
    EXPECT_EQ(parseImportDate("01.01.1899", false), std::nullopt);
}

TEST(ImportValuesTest, ExcelSerialDatesOnlyFromXlsx) {
    // 46083 = 2026-03-02 (days since 1899-12-30); 1 = 1900-01-01; 61 = 1900-03-01.
    EXPECT_EQ(parseImportDate("46083", true), ymd(2026, 3, 2));
    EXPECT_EQ(parseImportDate("46083.75", true), ymd(2026, 3, 2));
    EXPECT_EQ(parseImportDate("1", true), ymd(1900, 1, 1));
    EXPECT_EQ(parseImportDate("61", true), ymd(1900, 3, 1));
    EXPECT_EQ(parseImportDate("46083", false), std::nullopt);
    EXPECT_EQ(parseImportDate("0", true), std::nullopt);
}

TEST(ImportValuesTest, Times) {
    EXPECT_EQ(parseImportTime("18:00"), TimeOfDay{18 * 60});
    EXPECT_EQ(parseImportTime("8:30"), TimeOfDay{8 * 60 + 30});
    EXPECT_EQ(parseImportTime("18.15"), TimeOfDay{18 * 60 + 15});
    EXPECT_EQ(parseImportTime("18:00:00"), TimeOfDay{18 * 60});
    EXPECT_EQ(parseImportTime("24:00"), std::nullopt);
    EXPECT_EQ(parseImportTime("18"), std::nullopt);
}

TEST(ImportValuesTest, YesNo) {
    EXPECT_EQ(parseYesNo("TAK"), true);
    EXPECT_EQ(parseYesNo("x"), true);
    EXPECT_EQ(parseYesNo("1"), true);
    EXPECT_EQ(parseYesNo("Nie"), false);
    EXPECT_EQ(parseYesNo("no"), false);
    EXPECT_EQ(parseYesNo("-"), false);
    EXPECT_EQ(parseYesNo("może"), std::nullopt);
    EXPECT_EQ(parseYesNo(""), std::nullopt);
}

TEST(ImportValuesTest, WideFormatHasDatesAsHeaders) {
    EXPECT_TRUE(looksLikeWideFormat(table({"Zawodnik", "02.03.2026", "04.03.2026", "06.03.2026"},
                                          {{"Kowalski", "x", "x", ""}})));
    EXPECT_FALSE(looksLikeWideFormat(table(kTrainingHeaders, {})));
}

TEST(ImportValuesTest, WholeNumberCells) {
    EXPECT_EQ(readWholeNumber("7", 1, 10).value, 7);
    EXPECT_EQ(readWholeNumber("7,0", 1, 10).value, 7);
    EXPECT_EQ(readWholeNumber("6,5", 1, 10).issue, ActivityIssueKind::NotAWholeNumber);
    EXPECT_EQ(readWholeNumber("abc", 1, 10).issue, ActivityIssueKind::NotANumber);
    EXPECT_EQ(readWholeNumber("11", 1, 10).issue, ActivityIssueKind::OutOfRange);
    EXPECT_FALSE(readWholeNumber(" ", 1, 10).value.has_value());
    EXPECT_FALSE(readWholeNumber(" ", 1, 10).issue.has_value());
}

// --- Players ---

TEST(PlayerMatchingTest, ByNumberFullNameAndLastName) {
    const auto players = squad();
    EXPECT_EQ(matchPlayer("9", std::nullopt, players).id, PlayerId{1});
    EXPECT_EQ(matchPlayer("Jan Kowalski", std::nullopt, players).id, PlayerId{1});
    EXPECT_EQ(matchPlayer("KOWALSKI jan", std::nullopt, players).id, PlayerId{1});
    EXPECT_EQ(matchPlayer("Kowalski", std::nullopt, players).id, PlayerId{1});
    EXPECT_EQ(matchPlayer("Lukasz Wisniewski", std::nullopt, players).id, PlayerId{4});
    EXPECT_EQ(matchPlayer("", 4, players).id, PlayerId{2});
}

TEST(PlayerMatchingTest, AmbiguousAndUnknown) {
    const auto players = squad();
    const PlayerLookup nowak = matchPlayer("Nowak", std::nullopt, players);
    EXPECT_EQ(nowak.kind, PlayerLookupKind::Ambiguous);
    EXPECT_EQ(nowak.candidates.size(), 2U);
    // The number tells the two apart.
    EXPECT_EQ(matchPlayer("Nowak", 5, players).id, PlayerId{3});
    // A name of one player with the number of another.
    EXPECT_EQ(matchPlayer("Jan Kowalski", 4, players).kind, PlayerLookupKind::Ambiguous);
    EXPECT_EQ(matchPlayer("Zieliński", std::nullopt, players).kind, PlayerLookupKind::Unknown);
    EXPECT_EQ(matchPlayer("77", std::nullopt, players).kind, PlayerLookupKind::Unknown);
}

// --- Training ---

TEST(TrainingImportTest, AutoMapsPolishHeadersAndMetrics) {
    const std::vector<MetricDefinition> metrics = {
        metric(7, "Sprint 30 m", "sprint_30_m", MetricScope::Training)};
    std::vector<std::string> headers = kTrainingHeaders;
    headers.emplace_back("sprint 30m");
    const ActivityMapping mapping = autoMapTraining(headers, metrics);
    ASSERT_EQ(mapping.columns.size(), headers.size());
    EXPECT_EQ(mapping.columns[0].field, static_cast<int>(TrainingField::Date));
    EXPECT_EQ(mapping.columns[4].field, static_cast<int>(TrainingField::Player));
    EXPECT_EQ(mapping.columns[5].field, static_cast<int>(TrainingField::Status));
    EXPECT_EQ(mapping.columns[8].metric, MetricDefinitionId{7});
    EXPECT_TRUE(checkTrainingMapping(mapping).empty());
}

TEST(TrainingImportTest, MappingNeedsDateAndPlayer) {
    const ActivityMapping mapping = autoMapTraining(std::vector<std::string>{"RPE"}, {});
    const auto issues = checkTrainingMapping(mapping);
    EXPECT_NE(std::ranges::find(issues, ActivityMappingIssue::MissingDate), issues.end());
    EXPECT_NE(std::ranges::find(issues, ActivityMappingIssue::MissingPlayer), issues.end());
}

TEST(TrainingImportTest, RowsAreCheckedAndGrouped) {
    const SourceTable file =
        table(kTrainingHeaders,
              {
                  {"02.03.2026", "18:00", "", "trening", "Kowalski", "obecny", "6", "90"},
                  {"02.03.2026", "18:00", "", "trening", "Piotr Nowak", "", "5", "60"},
                  {"02.03.2026", "18:00", "", "", "Wiśniewski", "nieobecny", "4", ""},
                  {"31.02.2026", "", "", "", "Kowalski", "obecny", "11", "abc"},
                  {"04.03.2026", "", "", "", "Jan Przykładowy", "obecny", "6", "90"},
                  {"04.03.2026", "", "", "", "Kowalski", "", "", ""},
              });
    const TrainingImport result =
        validateTraining(file, autoMapTraining(file.headers, {}), squad(), {}, false);
    ASSERT_EQ(result.rows.size(), 6U);
    EXPECT_EQ(result.rows[0].status(), RowStatus::Ok);
    EXPECT_EQ(result.rows[0].rpe, 6);
    // No status but an RPE: present.
    EXPECT_EQ(result.rows[1].attendance, AttendanceStatus::Present);
    // Absent with an RPE: the values go, with a warning.
    EXPECT_EQ(result.rows[2].status(), RowStatus::Warning);
    EXPECT_TRUE(hasIssue(result.rows[2].issues, ActivityIssueKind::ValuesDropped));
    EXPECT_FALSE(result.rows[2].rpe.has_value());
    // A bad date, an RPE above 10 and minutes that are no number.
    EXPECT_TRUE(hasIssue(result.rows[3].issues, ActivityIssueKind::NotADate));
    EXPECT_TRUE(hasIssue(result.rows[3].issues, ActivityIssueKind::OutOfRange));
    EXPECT_TRUE(hasIssue(result.rows[3].issues, ActivityIssueKind::NotANumber));
    EXPECT_TRUE(result.rows[4].templateExample);
    EXPECT_TRUE(result.rows[5].nothingToImport);

    ASSERT_EQ(result.sessions.size(), 2U);
    EXPECT_EQ(result.sessions[0].rows.size(), 3U);
    EXPECT_EQ(result.sessions[0].maxMinutes, 90);
    EXPECT_EQ(sessionDuration(result.sessions[0]), 90);
    EXPECT_EQ(result.sessions[0].type, SessionType::Training);
}

TEST(TrainingImportTest, DuplicatesAndDifferentDurationsInOneSession) {
    const SourceTable file =
        table(kTrainingHeaders, {
                                    {"02.03.2026", "", "90", "", "Kowalski", "obecny", "6", "90"},
                                    {"02.03.2026", "", "75", "", "9", "obecny", "6", "90"},
                                });
    const TrainingImport result =
        validateTraining(file, autoMapTraining(file.headers, {}), squad(), {}, false);
    EXPECT_TRUE(hasIssue(result.rows[0].issues, ActivityIssueKind::DuplicateInFile));
    EXPECT_TRUE(hasIssue(result.rows[1].issues, ActivityIssueKind::DuplicateInFile));
    EXPECT_TRUE(hasIssue(result.rows[0].issues, ActivityIssueKind::Inconsistent));
    EXPECT_TRUE(hasIssue(result.rows[1].issues, ActivityIssueKind::Inconsistent));
    const auto other = std::ranges::find(result.rows[1].issues, ActivityIssueKind::Inconsistent,
                                         &ActivityIssue::kind);
    EXPECT_EQ(other->otherRows, std::vector<int>{2});
}

TEST(TrainingImportTest, DurationFromTheMinutesAndOverrides) {
    TrainingSessionGroup group;
    EXPECT_EQ(sessionDuration(group), 90);
    group.maxMinutes = 75;
    EXPECT_EQ(sessionDuration(group), 75);
    group.maxMinutes = 10;
    EXPECT_EQ(sessionDuration(group), 15);
    group.duration = 120;
    EXPECT_EQ(sessionDuration(group), 120);
}

TEST(TrainingImportTest, PlanCreatesSessionsAndOverridesOtherMinutes) {
    const SourceTable file =
        table(kTrainingHeaders,
              {
                  {"02.03.2026", "18:00", "", "", "Kowalski", "obecny", "6", "75"},
                  {"02.03.2026", "18:00", "", "", "Piotr Nowak", "obecny", "5", "60"},
                  {"02.03.2026", "18:00", "", "", "Marek Zieliński", "obecny", "5", "75"},
              });
    const auto players = squad();
    const TrainingImport import =
        validateTraining(file, autoMapTraining(file.headers, {}), players, {}, false);
    const TrainingPlan plan = planTraining(import, {}, players, {}, ActivityOptions{});
    ASSERT_EQ(plan.sessions.size(), 1U);
    EXPECT_FALSE(plan.sessions[0].existing.has_value());
    EXPECT_EQ(plan.sessions[0].draft.durationMin, 75);
    EXPECT_EQ(plan.sessions[0].draft.startTime, TimeOfDay{18 * 60});
    EXPECT_TRUE(plan.sessions[0].written);
    EXPECT_EQ(plan.rows[0].action, ActivityAction::Create);
    EXPECT_FALSE(plan.rows[0].attendance->minutesOverride.has_value());
    EXPECT_EQ(plan.rows[1].attendance->minutesOverride, 60);
    // Unknown player, skipped by default.
    EXPECT_EQ(plan.rows[2].action, ActivityAction::Skip);
    EXPECT_TRUE(hasIssue(plan.rows[2].issues, ActivityIssueKind::UnknownPlayer));

    ActivityOptions create;
    create.unknownPlayers = UnknownPlayerPolicy::Create;
    create.defaultPosition = Position::Cm;
    const TrainingPlan created = planTraining(import, {}, players, {}, create);
    ASSERT_EQ(created.newPlayers.size(), 1U);
    EXPECT_EQ(created.newPlayers[0].draft.lastName, "Zieliński");
    EXPECT_EQ(created.rows[2].action, ActivityAction::Create);
    EXPECT_EQ(created.rows[2].player.newPlayer, 0U);
}

TEST(TrainingImportTest, PlanAgainstExistingSession) {
    const SourceTable file =
        table(kTrainingHeaders, {
                                    {"02.03.2026", "", "", "", "Kowalski", "obecny", "6", "90"},
                                    {"02.03.2026", "", "", "", "Piotr Nowak", "obecny", "7", "90"},
                                    {"02.03.2026", "", "", "", "Adam Nowak", "obecny", "5", "90"},
                                });
    const auto players = squad();
    const TrainingImport import =
        validateTraining(file, autoMapTraining(file.headers, {}), players, {}, false);
    ExistingSession stored;
    stored.session.id = TrainingSessionId{50};
    stored.session.date = ymd(2026, 3, 2);
    stored.session.startTime = TimeOfDay{17 * 60};
    stored.session.durationMin = 90;
    Attendance same;
    same.playerId = PlayerId{1};
    same.status = AttendanceStatus::Present;
    same.rpe = 6;
    Attendance other = same;
    other.playerId = PlayerId{2};
    other.rpe = 4;
    stored.attendance = {same, other};
    const std::vector<ExistingSession> existing = {stored};

    const TrainingPlan kept = planTraining(import, existing, players, {}, ActivityOptions{});
    EXPECT_EQ(kept.sessions[0].existing, TrainingSessionId{50});
    EXPECT_EQ(kept.rows[0].action, ActivityAction::Unchanged);
    EXPECT_EQ(kept.rows[1].action, ActivityAction::KeepExisting);
    EXPECT_TRUE(hasIssue(kept.rows[1].issues, ActivityIssueKind::ExistingKept));
    EXPECT_EQ(kept.rows[2].action, ActivityAction::Create);

    ActivityOptions update;
    update.updateExisting = true;
    const TrainingPlan updated = planTraining(import, existing, players, {}, update);
    EXPECT_EQ(updated.rows[1].action, ActivityAction::Update);
    EXPECT_EQ(updated.rows[1].previousAttendance->rpe, 4);

    // A second session that day: without a start time the rows cannot choose.
    ExistingSession second = stored;
    second.session.id = TrainingSessionId{51};
    second.session.startTime = TimeOfDay{9 * 60};
    second.attendance.clear();
    const std::vector<ExistingSession> two = {stored, second};
    const TrainingPlan several = planTraining(import, two, players, {}, ActivityOptions{});
    EXPECT_EQ(several.rows[0].action, ActivityAction::Invalid);
    EXPECT_TRUE(hasIssue(several.rows[0].issues, ActivityIssueKind::SeveralSessions));
}

// --- Matches ---

namespace {

const std::vector<std::string> kMatchHeaders = {
    "Data",   "Przeciwnik", "Miejsce", "Rozgrywki",    "Wynik",           "Zawodnik", "Podstawowy",
    "Minuty", "Gole",       "Asysty",  "Żółte kartki", "Czerwone kartki", "Ocena",    "RPE"};

} // namespace

TEST(MatchImportTest, VenueAndScore) {
    EXPECT_EQ(parseVenue("u siebie"), Venue::Home);
    EXPECT_EQ(parseVenue("Wyjazd"), Venue::Away);
    EXPECT_EQ(parseVenue("neutral"), Venue::Neutral);
    EXPECT_EQ(parseVenue("?"), std::nullopt);
    EXPECT_EQ(parseScore("2:1"), (std::pair{2, 1}));
    EXPECT_EQ(parseScore("0 - 3"), (std::pair{0, 3}));
    EXPECT_EQ(parseScore("1\xE2\x80\x93"
                         "1"),
              (std::pair{1, 1}));
    EXPECT_EQ(parseScore("2"), std::nullopt);
}

TEST(MatchImportTest, AppearanceMinutes) {
    const AppearanceDraft starter = appearanceFor(true, 90, 90);
    EXPECT_EQ(starter.minuteOn, 0);
    EXPECT_EQ(starter.minuteOff, 90);
    const AppearanceDraft off = appearanceFor(true, 62, 90);
    EXPECT_EQ(off.minuteOff, 62);
    const AppearanceDraft substitute = appearanceFor(false, 28, 90);
    EXPECT_EQ(substitute.minuteOn, 62);
    EXPECT_EQ(substitute.minuteOff, 90);
    EXPECT_FALSE(substitute.started);
    const AppearanceDraft bench = appearanceFor(false, 0, 80);
    EXPECT_FALSE(bench.minuteOn.has_value());
    EXPECT_FALSE(bench.minuteOff.has_value());
}

TEST(MatchImportTest, RowsAreCheckedAndGrouped) {
    const SourceTable file = table(
        kMatchHeaders,
        {
            {"07.03.2026", "Wisła", "dom", "Liga", "2:1", "Kowalski", "tak", "80", "1", "", "1", "",
             "7", "8"},
            {"07.03.2026", "wisla", "", "", "", "Piotr Nowak", "nie", "25", "", "1", "", "", "6",
             "6"},
            {"07.03.2026", "Wisła", "", "", "", "Adam Nowak", "nie", "", "", "", "", "", "6,5", ""},
            {"07.03.2026", "Wisła", "", "", "", "Wiśniewski", "tak", "", "", "", "", "", "", ""},
        });
    const MatchImport result =
        validateMatches(file, autoMapMatches(file.headers, {}), squad(), {}, false);
    ASSERT_EQ(result.matches.size(), 1U);
    EXPECT_EQ(result.matches[0].rows.size(), 4U);
    EXPECT_EQ(result.rows[0].status(), RowStatus::Warning); // goals and a card not imported
    EXPECT_TRUE(hasIssue(result.rows[0].issues, ActivityIssueKind::EventsNotImported));
    // A rating 6,5 of a player who did not play: not a whole number (an error first).
    EXPECT_TRUE(hasIssue(result.rows[2].issues, ActivityIssueKind::NotAWholeNumber));
    EXPECT_TRUE(hasIssue(result.rows[3].issues, ActivityIssueKind::StartedWithoutMinutes));
    EXPECT_EQ(result.matches[0].maxMinutes, 80);
    EXPECT_EQ(result.matches[0].venue, Venue::Home);
    EXPECT_EQ(result.matches[0].score, (std::pair{2, 1}));
}

TEST(MatchImportTest, AnotherScoreInTheSameMatchIsAnError) {
    const SourceTable file = table(
        kMatchHeaders,
        {
            {"07.03.2026", "Wisła", "", "", "2:1", "Kowalski", "tak", "90", "", "", "", "", "", ""},
            {"07.03.2026", "Wisła", "", "", "3:1", "Piotr Nowak", "tak", "90", "", "", "", "", "",
             ""},
        });
    const MatchImport result =
        validateMatches(file, autoMapMatches(file.headers, {}), squad(), {}, false);
    EXPECT_TRUE(hasIssue(result.rows[0].issues, ActivityIssueKind::Inconsistent));
    EXPECT_TRUE(hasIssue(result.rows[1].issues, ActivityIssueKind::Inconsistent));
    EXPECT_FALSE(result.matches[0].score.has_value());
    // No venue in the match: home, with a warning on its first valid row (none here).
    EXPECT_FALSE(result.matches[0].venue.has_value());
}

TEST(MatchImportTest, PlanCreatesTheMatchAndAppearances) {
    const SourceTable file = table(
        kMatchHeaders,
        {
            {"07.03.2026", "Wisła", "wyjazd", "", "2:1", "Kowalski", "tak", "90", "2", "", "1", "",
             "7", "8"},
            {"07.03.2026", "Wisła", "", "", "", "Piotr Nowak", "nie", "30", "", "1", "", "", "6",
             "6"},
            {"07.03.2026", "Wisła", "", "", "", "Adam Nowak", "nie", "", "", "", "", "", "", ""},
        });
    const auto players = squad();
    const MatchImport import =
        validateMatches(file, autoMapMatches(file.headers, {}), players, {}, false);
    const MatchPlan plan = planMatches(import, {}, {}, players, {}, ActivityOptions{});
    ASSERT_EQ(plan.matches.size(), 1U);
    EXPECT_EQ(plan.newOpponents, std::vector<std::string>{"Wisła"});
    EXPECT_EQ(plan.matches[0].draft.venue, Venue::Away);
    EXPECT_EQ(plan.matches[0].goalsFor, 2);
    EXPECT_EQ(plan.matches[0].finalMinute, 90);
    EXPECT_EQ(plan.rows[1].appearance->minuteOn, 60);
    EXPECT_EQ(plan.rows[1].appearance->rating, 6);
    EXPECT_FALSE(plan.rows[2].appearance->minuteOn.has_value());
    const NotImportedEvents events = notImportedEvents(import, plan);
    EXPECT_EQ(events.goals, 2);
    EXPECT_EQ(events.assists, 1);
    EXPECT_EQ(events.cards, 1);
    EXPECT_EQ(events.yellowCards, 1);
}

TEST(MatchImportTest, PlanAgainstExistingMatches) {
    const SourceTable file = table(
        kMatchHeaders,
        {
            {"07.03.2026", "WISŁA", "", "", "3:0", "Kowalski", "tak", "90", "", "", "", "", "", ""},
        });
    const auto players = squad();
    const MatchImport import =
        validateMatches(file, autoMapMatches(file.headers, {}), players, {}, false);
    Opponent wisla;
    wisla.id = OpponentId{3};
    wisla.name = "Wisła";
    const std::vector<Opponent> opponents = {wisla};
    ExistingMatch stored;
    stored.match.id = MatchId{20};
    stored.match.opponentId = OpponentId{3};
    stored.match.date = ymd(2026, 3, 7);
    stored.match.status = MatchStatus::Finished;
    stored.match.goalsFor = 2;
    stored.match.goalsAgainst = 1;

    const MatchPlan finished = planMatches(import, opponents, std::vector{stored}, players, {}, {});
    EXPECT_EQ(finished.matches[0].existing, MatchId{20});
    EXPECT_TRUE(finished.newOpponents.empty());
    EXPECT_EQ(finished.rows[0].action, ActivityAction::Create);
    EXPECT_TRUE(hasIssue(finished.rows[0].issues, ActivityIssueKind::ScoreDiffers));

    stored.recorded = true;
    const MatchPlan recorded = planMatches(import, opponents, std::vector{stored}, players, {}, {});
    EXPECT_EQ(recorded.rows[0].action, ActivityAction::Invalid);
    EXPECT_TRUE(hasIssue(recorded.rows[0].issues, ActivityIssueKind::MatchRecorded));

    stored.recorded = false;
    stored.match.status = MatchStatus::Planned;
    const MatchPlan planned = planMatches(import, opponents, std::vector{stored}, players, {}, {});
    EXPECT_TRUE(hasIssue(planned.rows[0].issues, ActivityIssueKind::MatchPlanned));
}

// Review of P18: a group that disagrees is wholly an error, so it writes no session.
TEST(TrainingImportTest, DisagreeingDurationsMakeEveryRowOfTheSessionAnError) {
    const SourceTable file = table(
        kTrainingHeaders, {
                              {"02.03.2026", "", "60", "", "Kowalski", "obecny", "6", "60"},
                              {"02.03.2026", "", "120", "", "Piotr Nowak", "obecny", "6", "60"},
                              {"02.03.2026", "", "60", "", "Adam Nowak", "obecny", "6", "60"},
                              {"02.03.2026", "", "", "", "Wiśniewski", "obecny", "6", "60"},
                          });
    const auto players = squad();
    const TrainingImport import =
        validateTraining(file, autoMapTraining(file.headers, {}), players, {}, false);
    for (const TrainingRow& row : import.rows) {
        EXPECT_TRUE(hasIssue(row.issues, ActivityIssueKind::Inconsistent)) << row.sourceRow;
    }
    // Rows 2 and 4 name row 3 (another value); row 3 and row 5 (no value) name row 2.
    const auto named = [&](std::size_t index) {
        return std::ranges::find(import.rows[index].issues, ActivityIssueKind::Inconsistent,
                                 &ActivityIssue::kind)
            ->otherRows;
    };
    EXPECT_EQ(named(0), std::vector<int>{3});
    EXPECT_EQ(named(1), std::vector<int>{2});
    EXPECT_EQ(named(2), std::vector<int>{3});
    EXPECT_EQ(named(3), std::vector<int>{2});
    const TrainingPlan plan = planTraining(import, {}, players, {}, ActivityOptions{});
    EXPECT_FALSE(plan.sessions[0].written);
    EXPECT_TRUE(std::ranges::all_of(plan.rows, [](const PlannedTrainingRow& row) {
        return row.action == ActivityAction::Invalid;
    }));
}

// Review of P18: the plan matches the players again against the squad of now.
TEST(TrainingImportTest, ThePlanUsesTheSquadOfNow) {
    const SourceTable file =
        table({"Data", "Nr", "RPE"}, {{"02.03.2026", "9", "6"}, {"02.03.2026", "4", "5"}});
    auto players = squad();
    const TrainingImport import =
        validateTraining(file, autoMapTraining(file.headers, {}), players, {}, false);
    ASSERT_EQ(import.rows[0].player.lookup.id, PlayerId{1});
    // Jan Kowalski and Piotr Nowak swap their numbers after the check.
    players[0].shirtNumber = 4;
    players[1].shirtNumber = 9;
    const TrainingPlan plan = planTraining(import, {}, players, {}, ActivityOptions{});
    EXPECT_EQ(plan.rows[0].player.existing, PlayerId{2});
    EXPECT_EQ(plan.rows[1].player.existing, PlayerId{1});
    EXPECT_TRUE(hasIssue(plan.rows[0].issues, ActivityIssueKind::PlanChanged));
}

// Review of P18: a metric that is gone or no longer takes the value makes the row an error.
TEST(TrainingImportTest, ThePlanUsesTheMetricsOfNow) {
    const std::vector<MetricDefinition> metrics = {
        metric(7, "Sprint 30 m", "sprint_30_m", MetricScope::Training)};
    std::vector<std::string> headers = {"Data", "Zawodnik", "Sprint 30 m"};
    const SourceTable file = table(headers, {{"02.03.2026", "Kowalski", "4,35"}});
    const auto players = squad();
    const TrainingImport import =
        validateTraining(file, autoMapTraining(file.headers, metrics), players, metrics, false);
    EXPECT_EQ(planTraining(import, {}, players, metrics, {}).rows[0].action,
              ActivityAction::Create);
    // Moved to the match scope: not among the training metrics any more.
    const TrainingPlan gone = planTraining(import, {}, players, {}, {});
    EXPECT_EQ(gone.rows[0].action, ActivityAction::Invalid);
    EXPECT_TRUE(hasIssue(gone.rows[0].issues, ActivityIssueKind::MetricChanged));
    // A new limit below the value.
    std::vector<MetricDefinition> narrower = metrics;
    narrower[0].max = 4.0;
    const TrainingPlan limited = planTraining(import, {}, players, narrower, {});
    EXPECT_TRUE(hasIssue(limited.rows[0].issues, ActivityIssueKind::MetricChanged));
    EXPECT_EQ(limited.metrics, narrower);
}
