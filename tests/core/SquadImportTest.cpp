#include "core/parsing/SquadImport.hpp"
#include "core/parsing/Csv.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace fdss::core;

namespace {

constexpr int kCurrentYear = 2026;

SourceTable table(std::string_view csv) {
    auto result = toSourceTable(parseCsv(csv, ';'));
    EXPECT_TRUE(result.has_value());
    return result.value_or(SourceTable{});
}

std::vector<SquadRow> validate(std::string_view csv, std::span<const Player> existing = {},
                               std::optional<Position> defaultPosition = std::nullopt) {
    const SourceTable source = table(csv);
    SquadMapping mapping = autoMapSquad(source.headers);
    mapping.defaultPosition = defaultPosition;
    return validateSquad(source, mapping, existing, kCurrentYear);
}

bool hasIssue(const SquadRow& row, RowIssueKind kind, SquadField field) {
    return std::ranges::any_of(row.issues, [&](const RowIssue& issue) {
        return issue.kind == kind && issue.field == field;
    });
}

const RowIssue* findIssue(const SquadRow& row, RowIssueKind kind) {
    const auto found = std::ranges::find(row.issues, kind, &RowIssue::kind);
    return found == row.issues.end() ? nullptr : &*found;
}

Player player(std::int64_t id, std::string first, std::string last, std::optional<int> number) {
    Player result;
    result.id = PlayerId{id};
    result.teamId = TeamId{1};
    result.firstName = std::move(first);
    result.lastName = std::move(last);
    result.shirtNumber = number;
    result.primaryPosition = Position::Cm;
    return result;
}

// --- Mapping ---

TEST(SquadMappingTest, PolishHeadersOfTheDoneWhenFile) {
    const std::vector<std::string> headers = {"Nazwisko", "Imię", "Nr", "Pozycja"};
    const SquadMapping mapping = autoMapSquad(headers);
    EXPECT_EQ(mapping.columns, (std::vector<std::optional<SquadField>>{
                                   SquadField::LastName, SquadField::FirstName,
                                   SquadField::ShirtNumber, SquadField::PrimaryPosition}));
}

TEST(SquadMappingTest, EnglishHeadersAndUnknownColumns) {
    const std::vector<std::string> headers = {"First name",     "Surname",  "Shirt number",
                                              "Year of birth",  "Position", "Other positions",
                                              "Preferred foot", "Phone"};
    const SquadMapping mapping = autoMapSquad(headers);
    EXPECT_EQ(mapping.columns,
              (std::vector<std::optional<SquadField>>{
                  SquadField::FirstName, SquadField::LastName, SquadField::ShirtNumber,
                  SquadField::BirthYear, SquadField::PrimaryPosition,
                  SquadField::SecondaryPositions, SquadField::Foot, std::nullopt}));
}

TEST(SquadMappingTest, FullNameOrderComesFromTheHeader) {
    const std::vector<std::string> lastFirst = {"Nazwisko i imię", "Nr"};
    EXPECT_EQ(autoMapSquad(lastFirst).columns[0], SquadField::FullName);
    EXPECT_EQ(autoMapSquad(lastFirst).nameOrder, NameOrder::LastFirst);
    const std::vector<std::string> firstLast = {"Imię i nazwisko"};
    EXPECT_EQ(autoMapSquad(firstLast).nameOrder, NameOrder::FirstLast);
    const std::vector<std::string> player = {"Zawodnik"};
    EXPECT_EQ(autoMapSquad(player).nameOrder, NameOrder::FirstLast);
    EXPECT_EQ(detectNameOrder("Surname, name"), NameOrder::LastFirst);
}

TEST(SquadMappingTest, NameIsTheOnlyRequiredField) {
    const SourceTable source = table("Nazwisko;Imię;Nr\nKowalski;Jan;7\n");
    SquadMapping mapping = autoMapSquad(source.headers);
    // No position column: a default position is needed, nothing else.
    EXPECT_TRUE(needsDefaultPosition(mapping, source));
    EXPECT_EQ(checkMapping(mapping, source),
              (std::vector<MappingIssue>{MappingIssue::MissingDefaultPosition}));
    mapping.defaultPosition = Position::Cm;
    EXPECT_TRUE(checkMapping(mapping, source).empty());
    mapping.columns[1].reset();
    EXPECT_EQ(checkMapping(mapping, source),
              (std::vector<MappingIssue>{MappingIssue::MissingName}));
}

TEST(SquadMappingTest, EmptyPositionCellNeedsTheDefault) {
    const SourceTable source = table("Imię;Nazwisko;Pozycja\nJan;Kowalski;\nAdam;Nowak;N\n");
    const SquadMapping mapping = autoMapSquad(source.headers);
    EXPECT_TRUE(needsDefaultPosition(mapping, source));
    const SourceTable complete = table("Imię;Nazwisko;Pozycja\nAdam;Nowak;N\n");
    EXPECT_FALSE(needsDefaultPosition(autoMapSquad(complete.headers), complete));
}

TEST(SquadMappingTest, ConflictingColumns) {
    const SourceTable source = table("Zawodnik;Nazwisko;Nr;Numer\nJan Kowalski;Kowalski;7;7\n");
    SquadMapping mapping = autoMapSquad(source.headers);
    mapping.defaultPosition = Position::Cm;
    mapping.columns[3] = SquadField::ShirtNumber;
    const auto issues = checkMapping(mapping, source);
    EXPECT_NE(std::ranges::find(issues, MappingIssue::DuplicateField), issues.end());
    EXPECT_NE(std::ranges::find(issues, MappingIssue::FullNameWithParts), issues.end());
}

// --- Full names ---

TEST(FullNameTest, BothOrders) {
    EXPECT_EQ(splitFullName("Jan Kowalski", NameOrder::FirstLast), (NameParts{"Jan", "Kowalski"}));
    EXPECT_EQ(splitFullName("Kowalski Jan", NameOrder::LastFirst), (NameParts{"Jan", "Kowalski"}));
}

TEST(FullNameTest, DoubleBarrelledLastName) {
    EXPECT_EQ(splitFullName("Nowak-Kowalski Jan", NameOrder::LastFirst),
              (NameParts{"Jan", "Nowak-Kowalski"}));
    EXPECT_EQ(splitFullName("Jan Nowak-Kowalski", NameOrder::FirstLast),
              (NameParts{"Jan", "Nowak-Kowalski"}));
}

TEST(FullNameTest, SeveralFirstNamesAndSpaces) {
    EXPECT_EQ(splitFullName("  Jan   Maria  Kowalski ", NameOrder::FirstLast),
              (NameParts{"Jan Maria", "Kowalski"}));
    EXPECT_EQ(splitFullName("Kowalski Jan Maria", NameOrder::LastFirst),
              (NameParts{"Jan Maria", "Kowalski"}));
}

TEST(FullNameTest, CommaSplits) {
    EXPECT_EQ(splitFullName("Kowalski, Jan Maria", NameOrder::LastFirst),
              (NameParts{"Jan Maria", "Kowalski"}));
}

TEST(FullNameTest, OneWordIsNotAName) {
    EXPECT_EQ(splitFullName("Kowalski", NameOrder::FirstLast), std::nullopt);
    EXPECT_EQ(splitFullName("Kowalski,", NameOrder::LastFirst), std::nullopt);
}

// --- Values ---

TEST(SquadValuesTest, WholeNumbers) {
    EXPECT_EQ(parseWholeNumber("7"), 7);
    EXPECT_EQ(parseWholeNumber(" 07 "), 7);
    EXPECT_EQ(parseWholeNumber("7.0"), 7);
    EXPECT_EQ(parseWholeNumber("7,00"), 7);
    EXPECT_EQ(parseWholeNumber("7.5"), std::nullopt);
    EXPECT_EQ(parseWholeNumber("abc"), std::nullopt);
    EXPECT_EQ(parseWholeNumber("-3"), std::nullopt);
    EXPECT_EQ(parseWholeNumber("1234567890"), std::nullopt);
}

TEST(SquadValuesTest, BirthYearFromAYearOrADate) {
    EXPECT_EQ(parseBirthYear("2001"), 2001);
    EXPECT_EQ(parseBirthYear("2001-05-12"), 2001);
    EXPECT_EQ(parseBirthYear("12.05.2001"), 2001);
    EXPECT_EQ(parseBirthYear("12/05/2001"), 2001);
    EXPECT_EQ(parseBirthYear("2001-05-12T00:00:00"), 2001);
    EXPECT_EQ(parseBirthYear("12.13.2001"), 2001); // month/day order unknown
    EXPECT_EQ(parseBirthYear("13.13.2001"), std::nullopt);
    EXPECT_EQ(parseBirthYear("May 2001"), std::nullopt);
}

// --- Rows ---

TEST(SquadRowsTest, ValidRowIsOk) {
    const auto rows = validate("Imię;Nazwisko;Nr;Rok urodzenia;Pozycja;Pozycje dodatkowe;Noga\n"
                               "Łukasz;Żółkiewski;9;2001;N;ŚPO, PS;L\n");
    ASSERT_EQ(rows.size(), 1U);
    const SquadRow& row = rows[0];
    EXPECT_EQ(row.status(), RowStatus::Ok);
    EXPECT_EQ(row.sourceRow, 2);
    EXPECT_EQ(row.draft.firstName, "Łukasz");
    EXPECT_EQ(row.draft.lastName, "Żółkiewski");
    EXPECT_EQ(row.draft.shirtNumber, 9);
    EXPECT_EQ(row.draft.birthYear, 2001);
    EXPECT_EQ(row.draft.primaryPosition, Position::St);
    EXPECT_EQ(row.draft.secondaryPositions, (std::vector<Position>{Position::Am, Position::Rw}));
    EXPECT_EQ(row.draft.foot, PreferredFoot::Left);
    EXPECT_EQ(row.match, PlayerMatch::New);
}

TEST(SquadRowsTest, ShirtNumberThatIsNotANumber) {
    const auto rows = validate("Imię;Nazwisko;Nr;Pozycja\nJan;Kowalski;abc;N\n");
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].status(), RowStatus::Error);
    const RowIssue* issue = findIssue(rows[0], RowIssueKind::NotANumber);
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->field, SquadField::ShirtNumber);
    EXPECT_EQ(issue->value, "abc");
}

TEST(SquadRowsTest, RangesOfNumberAndYear) {
    const auto rows = validate("Imię;Nazwisko;Nr;Rok urodzenia;Pozycja\n"
                               "Jan;Kowalski;100;2001;N\n"
                               "Adam;Nowak;5;2023;N\n"
                               "Piotr;Zieliński;6;1949;N\n");
    EXPECT_TRUE(hasIssue(rows[0], RowIssueKind::OutOfRange, SquadField::ShirtNumber));
    EXPECT_TRUE(hasIssue(rows[1], RowIssueKind::OutOfRange, SquadField::BirthYear));
    EXPECT_TRUE(hasIssue(rows[2], RowIssueKind::OutOfRange, SquadField::BirthYear));
    const auto ok = validate("Imię;Nazwisko;Rok urodzenia;Pozycja\nJan;Kowalski;2022;N\n");
    EXPECT_EQ(ok[0].status(), RowStatus::Ok);
}

TEST(SquadRowsTest, MissingAndTooLongNames) {
    const std::string longName(51, 'a');
    const auto rows = validate("Imię;Nazwisko;Pozycja\n;Kowalski;N\nJan;" + longName + ";N\n");
    EXPECT_TRUE(hasIssue(rows[0], RowIssueKind::MissingValue, SquadField::FirstName));
    EXPECT_TRUE(hasIssue(rows[1], RowIssueKind::TooLong, SquadField::LastName));
}

TEST(SquadRowsTest, UnknownPositionIsAnErrorUnknownSecondaryAWarning) {
    const auto rows = validate("Imię;Nazwisko;Pozycja;Pozycje dodatkowe\n"
                               "Jan;Kowalski;XYZ;\n"
                               "Adam;Nowak;N;ŚO, QQ\n");
    EXPECT_EQ(rows[0].status(), RowStatus::Error);
    const RowIssue* unknown = findIssue(rows[0], RowIssueKind::UnknownPosition);
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ(unknown->value, "XYZ");
    EXPECT_EQ(rows[1].status(), RowStatus::Warning);
    EXPECT_TRUE(hasIssue(rows[1], RowIssueKind::UnknownPosition, SquadField::SecondaryPositions));
    EXPECT_EQ(rows[1].draft.secondaryPositions, (std::vector<Position>{Position::Cb}));
}

TEST(SquadRowsTest, ListInThePositionCell) {
    const auto rows = validate("Imię;Nazwisko;Pozycja\nJan;Kowalski;ŚO/PO\n");
    EXPECT_EQ(rows[0].status(), RowStatus::Ok);
    EXPECT_EQ(rows[0].draft.primaryPosition, Position::Cb);
    EXPECT_EQ(rows[0].draft.secondaryPositions, (std::vector<Position>{Position::Rb}));
}

TEST(SquadRowsTest, FileWithoutPositionUsesTheDefaultWithAWarning) {
    const auto rows =
        validate("Nazwisko;Imię;Nr\nKowalski;Jan;7\nNowak;Adam;9\n", {}, Position::Cm);
    ASSERT_EQ(rows.size(), 2U);
    for (const SquadRow& row : rows) {
        EXPECT_EQ(row.status(), RowStatus::Warning);
        EXPECT_EQ(row.draft.primaryPosition, Position::Cm);
        EXPECT_FALSE(row.filled.primaryPosition);
        EXPECT_TRUE(hasIssue(row, RowIssueKind::DefaultPosition, SquadField::PrimaryPosition));
    }
}

TEST(SquadRowsTest, EmptyPositionCellUsesTheDefault) {
    const auto rows =
        validate("Imię;Nazwisko;Pozycja\nJan;Kowalski;\nAdam;Nowak;BR\n", {}, Position::St);
    EXPECT_EQ(rows[0].draft.primaryPosition, Position::St);
    EXPECT_TRUE(hasIssue(rows[0], RowIssueKind::DefaultPosition, SquadField::PrimaryPosition));
    EXPECT_EQ(rows[1].status(), RowStatus::Ok);
    EXPECT_EQ(rows[1].draft.primaryPosition, Position::Gk);
}

TEST(SquadRowsTest, UnknownFootIsAWarning) {
    const auto rows = validate("Imię;Nazwisko;Pozycja;Noga\nJan;Kowalski;N;ręka\nA;B;N;\n");
    EXPECT_EQ(rows[0].status(), RowStatus::Warning);
    EXPECT_EQ(rows[0].draft.foot, PreferredFoot::Unknown);
    EXPECT_TRUE(hasIssue(rows[0], RowIssueKind::UnknownFoot, SquadField::Foot));
    EXPECT_EQ(rows[1].draft.foot, PreferredFoot::Unknown);
    EXPECT_EQ(rows[1].status(), RowStatus::Ok);
}

TEST(SquadRowsTest, FullNameColumnInBothOrders) {
    const SourceTable source = table("Zawodnik;Pozycja\nNowak-Kowalski Jan;N\nKowalski;N\n");
    SquadMapping mapping = autoMapSquad(source.headers);
    mapping.nameOrder = NameOrder::LastFirst;
    auto rows = validateSquad(source, mapping, {}, kCurrentYear);
    EXPECT_EQ(rows[0].draft.firstName, "Jan");
    EXPECT_EQ(rows[0].draft.lastName, "Nowak-Kowalski");
    EXPECT_TRUE(hasIssue(rows[1], RowIssueKind::SingleWordName, SquadField::FullName));
    mapping.nameOrder = NameOrder::FirstLast;
    rows = validateSquad(source, mapping, {}, kCurrentYear);
    EXPECT_EQ(rows[0].draft.firstName, "Nowak-Kowalski");
    EXPECT_EQ(rows[0].draft.lastName, "Jan");
}

TEST(SquadRowsTest, DuplicatesInTheFileNameEachOther) {
    const auto rows = validate("Imię;Nazwisko;Nr;Pozycja\n"
                               "Jan;Kowalski;7;N\n"
                               "Adam;Nowak;7;N\n"
                               "jan;KOWALSKI;8;N\n"
                               "Piotr;Lis;10;N\n");
    const RowIssue* number = findIssue(rows[0], RowIssueKind::DuplicateInFile);
    ASSERT_NE(number, nullptr);
    EXPECT_TRUE(hasIssue(rows[0], RowIssueKind::DuplicateInFile, SquadField::ShirtNumber));
    EXPECT_TRUE(hasIssue(rows[0], RowIssueKind::DuplicateInFile, SquadField::LastName));
    EXPECT_TRUE(hasIssue(rows[1], RowIssueKind::DuplicateInFile, SquadField::ShirtNumber));
    EXPECT_TRUE(hasIssue(rows[2], RowIssueKind::DuplicateInFile, SquadField::LastName));
    EXPECT_EQ(rows[3].status(), RowStatus::Ok);
    const auto nameIssue = std::ranges::find_if(rows[2].issues, [](const RowIssue& issue) {
        return issue.kind == RowIssueKind::DuplicateInFile;
    });
    EXPECT_EQ(nameIssue->otherRows, (std::vector<int>{2}));
}

// --- Existing players ---

TEST(SquadMatchTest, SameNameIsTheExistingPlayer) {
    const std::vector existing = {player(1, "Jan", "Kowalski", 7)};
    const auto rows =
        validate("Imię;Nazwisko;Nr;Pozycja\njan;kowalski;;N\nJan;Kowalski;7;N\n", existing);
    // Rows 1 and 2 are duplicates of each other; check the matching on a file of one row.
    const auto single = validate("Imię;Nazwisko;Nr;Pozycja\njan;kowalski;;N\n", existing);
    EXPECT_EQ(single[0].match, PlayerMatch::Existing);
    EXPECT_EQ(single[0].existing, PlayerId{1});
    EXPECT_EQ(rows[0].status(), RowStatus::Error);
}

TEST(SquadMatchTest, SameNumberAndLastName) {
    const std::vector existing = {player(1, "Jakub", "Kowalski", 7)};
    const auto rows = validate("Imię;Nazwisko;Nr;Pozycja\nKuba;Kowalski;7;N\n", existing);
    EXPECT_EQ(rows[0].match, PlayerMatch::Existing);
    EXPECT_EQ(rows[0].existing, PlayerId{1});
}

TEST(SquadMatchTest, NumberOfAnotherPlayerIsAmbiguous) {
    const std::vector existing = {player(1, "Jan", "Kowalski", 7)};
    const auto rows = validate("Imię;Nazwisko;Nr;Pozycja\nAdam;Nowak;7;N\n", existing);
    EXPECT_EQ(rows[0].match, PlayerMatch::Ambiguous);
    EXPECT_EQ(rows[0].status(), RowStatus::Warning);
    const RowIssue* issue = findIssue(rows[0], RowIssueKind::AmbiguousMatch);
    ASSERT_NE(issue, nullptr);
    EXPECT_EQ(issue->field, SquadField::ShirtNumber);
    ASSERT_EQ(issue->candidates.size(), 1U);
    EXPECT_EQ(issue->candidates[0].lastName, "Kowalski");
    EXPECT_EQ(actionFor(rows[0], ExistingPolicy::Update), RowAction::SkipAmbiguous);
}

TEST(SquadMatchTest, NameAndNumberOfTwoPlayersIsAmbiguous) {
    const std::vector existing = {player(1, "Jan", "Kowalski", 7), player(2, "Adam", "Nowak", 9)};
    const auto rows = validate("Imię;Nazwisko;Nr;Pozycja\nJan;Kowalski;9;N\n", existing);
    EXPECT_EQ(rows[0].match, PlayerMatch::Ambiguous);
    EXPECT_EQ(findIssue(rows[0], RowIssueKind::AmbiguousMatch)->candidates.size(), 2U);
}

TEST(SquadMatchTest, TwoPlayersOfTheSameNameNeedTheNumber) {
    const std::vector existing = {player(1, "Jan", "Kowalski", 7),
                                  player(2, "Jan", "Kowalski", 17)};
    EXPECT_EQ(validate("Imię;Nazwisko;Pozycja\nJan;Kowalski;N\n", existing)[0].match,
              PlayerMatch::Ambiguous);
    const auto byNumber = validate("Imię;Nazwisko;Nr;Pozycja\nJan;Kowalski;17;N\n", existing);
    EXPECT_EQ(byNumber[0].match, PlayerMatch::Existing);
    EXPECT_EQ(byNumber[0].existing, PlayerId{2});
}

TEST(SquadMatchTest, ActionsFollowThePolicy) {
    const std::vector existing = {player(1, "Jan", "Kowalski", 7)};
    const auto rows = validate("Imię;Nazwisko;Nr;Pozycja\nJan;Kowalski;7;N\nAdam;Lis;;N\n"
                               "Piotr;Zając;abc;N\n",
                               existing);
    EXPECT_EQ(actionFor(rows[0], ExistingPolicy::Update), RowAction::Update);
    EXPECT_EQ(actionFor(rows[0], ExistingPolicy::Skip), RowAction::SkipExisting);
    EXPECT_EQ(actionFor(rows[1], ExistingPolicy::Skip), RowAction::Create);
    EXPECT_EQ(actionFor(rows[2], ExistingPolicy::Update), RowAction::Invalid);
}

TEST(SquadMatchTest, UpdateChangesOnlyFilledValues) {
    Player stored = player(1, "Jan", "Kowalski", 7);
    stored.birthYear = 1999;
    stored.primaryPosition = Position::Cb;
    stored.secondaryPositions = {Position::St};
    stored.foot = PreferredFoot::Left;
    stored.notes = "kapitan";
    const std::vector existing = {stored};
    const auto rows = validate("Imię;Nazwisko;Nr;Rok urodzenia;Noga\nJan;Kowalski;7;;P\n", existing,
                               Position::St);
    ASSERT_EQ(rows[0].match, PlayerMatch::Existing);
    const PlayerDraft merged = mergeUpdate(stored, rows[0]);
    EXPECT_EQ(merged.birthYear, 1999);               // empty cell
    EXPECT_EQ(merged.primaryPosition, Position::Cb); // the default does not overwrite
    EXPECT_EQ(merged.foot, PreferredFoot::Right);
    EXPECT_EQ(merged.notes, "kapitan");
    EXPECT_EQ(merged.secondaryPositions, (std::vector<Position>{Position::St}));
}

TEST(SquadMatchTest, ProgressCountsRows) {
    const SourceTable source = table("Imię;Nazwisko;Pozycja\nA;B;N\nC;D;N\nE;F;N\n");
    std::vector<std::size_t> calls;
    (void)validateSquad(source, autoMapSquad(source.headers), {}, kCurrentYear,
                        [&](std::size_t done) { calls.push_back(done); });
    EXPECT_EQ(calls, (std::vector<std::size_t>{1, 2, 3}));
}

} // namespace
