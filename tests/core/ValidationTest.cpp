#include "core/domain/Validation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <optional>
#include <string>

using namespace fdss::core;
using namespace std::chrono;

namespace {

constexpr int kCurrentYear = 2026;
constexpr Date kDate = 2026y / November / 10d;
constexpr Date kDayBefore = 2026y / November / 9d;
constexpr Date kImpossibleDate = 2026y / February / 30d;
constexpr std::array kSupportedEnds{1900y / January / 1d, 2100y / December / 31d};
constexpr std::array kUnsupportedDates{1899y / December / 31d, 2101y / January / 1d,
                                       0y / January / 1d};

// N characters, each of them two bytes in UTF-8.
std::string polishName(std::size_t length) {
    std::string name;
    for (std::size_t i = 0; i < length; ++i) {
        name += "ą";
    }
    return name;
}

bool hasField(const ValidationErrors& errors, std::string_view field) {
    return std::ranges::any_of(errors, [&](const ValidationError& error) {
        return error.field == field && error.code == ErrorCode::InvalidInput;
    });
}

PlayerDraft validPlayer() {
    PlayerDraft draft;
    draft.firstName = "Jan";
    draft.lastName = "Kowalski";
    return draft;
}

TeamDraft validTeam() {
    TeamDraft draft;
    draft.name = "Seniorzy";
    draft.season = "2026/27";
    return draft;
}

AvailabilityDraft validAvailability() {
    AvailabilityDraft draft;
    draft.fromDate = kDate;
    return draft;
}

MatchDraft validMatch() {
    MatchDraft draft;
    draft.date = kDate;
    return draft;
}

TrainingSessionDraft validSession() {
    TrainingSessionDraft draft;
    draft.date = kDate;
    return draft;
}

} // namespace

// --- Text helpers ---

TEST(ValidationTest, TrimmedRemovesAsciiWhitespaceFromBothEnds) {
    EXPECT_EQ(trimmed("  Jan \t\r\n"), "Jan");
    EXPECT_EQ(trimmed("Jan Maria"), "Jan Maria");
    EXPECT_EQ(trimmed(" \t "), "");
    EXPECT_EQ(trimmed(""), "");
    EXPECT_EQ(trimmed(" Śląsk "), "Śląsk");
}

TEST(ValidationTest, CharacterCountCountsCodePointsNotBytes) {
    EXPECT_EQ(characterCount(""), 0U);
    EXPECT_EQ(characterCount("abc"), 3U);
    EXPECT_EQ(characterCount("ą"), 1U);
    EXPECT_EQ(characterCount("Śląsk"), 5U);
    EXPECT_EQ(characterCount("€"), 1U);
    EXPECT_EQ(characterCount("😀"), 1U);
}

// --- Names ---

TEST(ValidationTest, PersonNamesAreOneToFiftyCharactersAfterTrim) {
    const auto errorsFor = [](const std::string& name) {
        PlayerDraft firstName = validPlayer();
        firstName.firstName = name;
        PlayerDraft lastName = validPlayer();
        lastName.lastName = name;
        StaffUserDraft staff;
        staff.name = name;
        return std::array{hasField(validate(firstName, kCurrentYear), "first_name"),
                          hasField(validate(lastName, kCurrentYear), "last_name"),
                          hasField(validate(staff), "name")};
    };
    for (const std::string& valid : {polishName(1), polishName(50), "  " + polishName(50) + "  "}) {
        for (const bool hasError : errorsFor(valid)) {
            EXPECT_FALSE(hasError) << valid;
        }
    }
    for (const std::string& invalid : {std::string{}, std::string("   "), polishName(51)}) {
        for (const bool hasError : errorsFor(invalid)) {
            EXPECT_TRUE(hasError) << invalid;
        }
    }
}

TEST(ValidationTest, OrganisationNamesAreOneToHundredCharactersAfterTrim) {
    const auto errorsFor = [](const std::string& name) {
        ClubDraft club;
        club.name = name;
        TeamDraft team = validTeam();
        team.name = name;
        OpponentDraft opponent;
        opponent.name = name;
        return std::array{hasField(validate(club), "name"), hasField(validate(team), "name"),
                          hasField(validate(opponent), "name")};
    };
    for (const std::string& valid :
         {polishName(1), polishName(51), polishName(100), " " + polishName(100) + " "}) {
        for (const bool hasError : errorsFor(valid)) {
            EXPECT_FALSE(hasError) << valid;
        }
    }
    for (const std::string& invalid : {std::string{}, std::string("   "), polishName(101)}) {
        for (const bool hasError : errorsFor(invalid)) {
            EXPECT_TRUE(hasError) << invalid;
        }
    }
}

// --- Player ---

TEST(ValidationTest, ValidPlayerHasNoErrors) {
    EXPECT_TRUE(validate(validPlayer(), kCurrentYear).empty());
}

TEST(ValidationTest, ShirtNumberIsOptionalAndOneToNinetyNine) {
    const auto hasError = [](std::optional<int> number) {
        PlayerDraft draft = validPlayer();
        draft.shirtNumber = number;
        return hasField(validate(draft, kCurrentYear), "shirt_number");
    };
    EXPECT_FALSE(hasError(std::nullopt));
    EXPECT_TRUE(hasError(0));
    EXPECT_FALSE(hasError(1));
    EXPECT_FALSE(hasError(99));
    EXPECT_TRUE(hasError(100));
}

TEST(ValidationTest, BirthYearIsOptionalAndBetween1950AndCurrentMinusFour) {
    const auto hasError = [](std::optional<int> year) {
        PlayerDraft draft = validPlayer();
        draft.birthYear = year;
        return hasField(validate(draft, kCurrentYear), "birth_year");
    };
    EXPECT_FALSE(hasError(std::nullopt));
    EXPECT_TRUE(hasError(1949));
    EXPECT_FALSE(hasError(1950));
    EXPECT_FALSE(hasError(kCurrentYear - 4));
    EXPECT_TRUE(hasError(kCurrentYear - 3));
}

TEST(ValidationTest, SecondaryPositionsHaveNoDuplicatesAndNotThePrimaryOne) {
    PlayerDraft draft = validPlayer();
    draft.primaryPosition = Position::Cm;
    draft.secondaryPositions = {Position::Dm, Position::Am};
    EXPECT_FALSE(hasField(validate(draft, kCurrentYear), "secondary_positions"));
    draft.secondaryPositions = {Position::Dm, Position::Am, Position::Dm};
    EXPECT_TRUE(hasField(validate(draft, kCurrentYear), "secondary_positions"));
    draft.secondaryPositions = {Position::Dm, Position::Cm};
    EXPECT_TRUE(hasField(validate(draft, kCurrentYear), "secondary_positions"));
}

TEST(ValidationTest, EveryBrokenPlayerFieldIsReported) {
    PlayerDraft draft;
    draft.shirtNumber = 0;
    draft.birthYear = 1900;
    const ValidationErrors errors = validate(draft, kCurrentYear);
    EXPECT_EQ(errors.size(), 4U);
    EXPECT_TRUE(hasField(errors, "first_name"));
    EXPECT_TRUE(hasField(errors, "last_name"));
    EXPECT_TRUE(hasField(errors, "shirt_number"));
    EXPECT_TRUE(hasField(errors, "birth_year"));
}

// --- Team ---

TEST(ValidationTest, SeasonIsTwoConsecutiveYearsFrom1900) {
    const auto hasError = [](const char* season) {
        TeamDraft draft = validTeam();
        draft.season = season;
        return hasField(validate(draft), "season");
    };
    for (const char* good : {"2026/27", "1999/00", "1900/01", "2099/00", "2009/10"}) {
        EXPECT_FALSE(hasError(good)) << good;
    }
    for (const char* bad : {"", "2026", "26/27", "2026-27", "2026/2027", "abcd/ef", "2026/28",
                            "2026/26", "2026/25", "1899/00", "2100/01", "0000/01", "9999/00"}) {
        EXPECT_TRUE(hasError(bad)) << bad;
    }
}

// --- Supported dates ---

TEST(ValidationTest, SupportedDatesAreRealDatesFrom1900To2100) {
    EXPECT_EQ(kMinSupportedDate, kSupportedEnds.front());
    EXPECT_EQ(kMaxSupportedDate, kSupportedEnds.back());
    EXPECT_TRUE(isSupportedDate(kDate));
    for (const Date date : kSupportedEnds) {
        EXPECT_TRUE(isSupportedDate(date));
    }
    for (const Date date : kUnsupportedDates) {
        EXPECT_FALSE(isSupportedDate(date));
    }
    EXPECT_FALSE(isSupportedDate(kImpossibleDate));
    EXPECT_FALSE(isSupportedDate(Date{}));
}

TEST(ValidationTest, EveryDateFieldAcceptsTheEndsOfTheSupportedRange) {
    for (const Date date : kSupportedEnds) {
        AvailabilityDraft availability;
        availability.fromDate = date;
        availability.expectedTo = date;
        availability.actualTo = date;
        EXPECT_TRUE(validate(availability).empty());

        MatchDraft match = validMatch();
        match.date = date;
        EXPECT_TRUE(validate(match).empty());

        TrainingSessionDraft session = validSession();
        session.date = date;
        EXPECT_TRUE(validate(session).empty());
    }
}

// All three availability dates are equal, so only the range can reject the end dates.
TEST(ValidationTest, EveryDateFieldRejectsDatesOutsideTheSupportedRange) {
    for (const Date date : kUnsupportedDates) {
        AvailabilityDraft availability;
        availability.fromDate = date;
        availability.expectedTo = date;
        availability.actualTo = date;
        const ValidationErrors errors = validate(availability);
        EXPECT_TRUE(hasField(errors, "from_date"));
        EXPECT_TRUE(hasField(errors, "expected_to"));
        EXPECT_TRUE(hasField(errors, "actual_to"));

        MatchDraft match = validMatch();
        match.date = date;
        EXPECT_TRUE(hasField(validate(match), "date"));

        TrainingSessionDraft session = validSession();
        session.date = date;
        EXPECT_TRUE(hasField(validate(session), "date"));
    }
}

// --- Availability ---

TEST(ValidationTest, AvailabilityDatesMustBeRealAndNotEndBeforeTheStart) {
    EXPECT_TRUE(validate(validAvailability()).empty());

    AvailabilityDraft draft = validAvailability();
    draft.fromDate = kImpossibleDate;
    EXPECT_TRUE(hasField(validate(draft), "from_date"));

    draft = validAvailability();
    draft.expectedTo = kDate;
    draft.actualTo = kDate;
    EXPECT_TRUE(validate(draft).empty());

    draft.expectedTo = kDayBefore;
    draft.actualTo = std::nullopt;
    EXPECT_TRUE(hasField(validate(draft), "expected_to"));

    draft.expectedTo = std::nullopt;
    draft.actualTo = kDayBefore;
    EXPECT_TRUE(hasField(validate(draft), "actual_to"));

    draft.actualTo = kImpossibleDate;
    EXPECT_TRUE(hasField(validate(draft), "actual_to"));
}

TEST(ValidationTest, EndDateIsNotComparedWithABrokenStart) {
    AvailabilityDraft draft;
    draft.fromDate = kImpossibleDate;
    draft.expectedTo = kDate;
    const ValidationErrors errors = validate(draft);
    EXPECT_TRUE(hasField(errors, "from_date"));
    EXPECT_FALSE(hasField(errors, "expected_to"));
}

// --- Match ---

TEST(ValidationTest, MatchDateAndKickoffAreChecked) {
    EXPECT_TRUE(validate(validMatch()).empty());

    MatchDraft draft = validMatch();
    draft.date = kImpossibleDate;
    EXPECT_TRUE(hasField(validate(draft), "date"));

    const auto kickoffHasError = [](TimeOfDay time) {
        MatchDraft match = validMatch();
        match.kickoffTime = time;
        return hasField(validate(match), "kickoff_time");
    };
    EXPECT_FALSE(kickoffHasError(minutes{0}));
    EXPECT_FALSE(kickoffHasError(minutes{1439}));
    EXPECT_TRUE(kickoffHasError(minutes{1440}));
    EXPECT_TRUE(kickoffHasError(minutes{-1}));
}

TEST(ValidationTest, AppearanceRangesAreChecked) {
    const auto errorsFor = [](auto change) {
        AppearanceDraft draft;
        change(draft);
        return validate(draft);
    };
    EXPECT_TRUE(errorsFor([](AppearanceDraft&) {}).empty());

    EXPECT_FALSE(hasField(errorsFor([](auto& d) { d.minuteOn = 0; }), "minute_on"));
    EXPECT_FALSE(hasField(errorsFor([](auto& d) { d.minuteOn = 130; }), "minute_on"));
    EXPECT_TRUE(hasField(errorsFor([](auto& d) { d.minuteOn = -1; }), "minute_on"));
    EXPECT_TRUE(hasField(errorsFor([](auto& d) { d.minuteOn = 131; }), "minute_on"));

    EXPECT_FALSE(hasField(errorsFor([](auto& d) { d.minuteOff = 130; }), "minute_off"));
    EXPECT_TRUE(hasField(errorsFor([](auto& d) { d.minuteOff = 131; }), "minute_off"));
    EXPECT_TRUE(hasField(errorsFor([](auto& d) {
                             d.minuteOn = 60;
                             d.minuteOff = 59;
                         }),
                         "minute_off"));
    EXPECT_FALSE(hasField(errorsFor([](auto& d) {
                              d.minuteOn = 60;
                              d.minuteOff = 60;
                          }),
                          "minute_off"));

    EXPECT_TRUE(hasField(errorsFor([](auto& d) { d.rating = 0; }), "rating"));
    EXPECT_FALSE(hasField(errorsFor([](auto& d) { d.rating = 1; }), "rating"));
    EXPECT_FALSE(hasField(errorsFor([](auto& d) { d.rating = 10; }), "rating"));
    EXPECT_TRUE(hasField(errorsFor([](auto& d) { d.rating = 11; }), "rating"));

    EXPECT_TRUE(hasField(errorsFor([](auto& d) { d.rpe = -1; }), "rpe"));
    EXPECT_FALSE(hasField(errorsFor([](auto& d) { d.rpe = 0; }), "rpe"));
    EXPECT_FALSE(hasField(errorsFor([](auto& d) { d.rpe = 10; }), "rpe"));
    EXPECT_TRUE(hasField(errorsFor([](auto& d) { d.rpe = 11; }), "rpe"));
}

TEST(ValidationTest, MatchEventMinuteAndPlayerAreChecked) {
    MatchEventDraft draft;
    draft.playerId = PlayerId{1};
    for (const int minute : {0, 130}) {
        draft.minute = minute;
        EXPECT_FALSE(hasField(validate(draft), "minute")) << minute;
    }
    for (const int minute : {-1, 131}) {
        draft.minute = minute;
        EXPECT_TRUE(hasField(validate(draft), "minute")) << minute;
    }

    draft.minute = 10;
    draft.playerId = std::nullopt;
    for (const MatchEventType type : allValues<MatchEventType>()) {
        draft.type = type;
        EXPECT_EQ(hasField(validate(draft), "player_id"), type != MatchEventType::OpponentGoal)
            << toDbText(type);
    }
}

// --- Training ---

TEST(ValidationTest, SessionFieldsAreChecked) {
    EXPECT_TRUE(validate(validSession()).empty());

    TrainingSessionDraft draft = validSession();
    draft.date = kImpossibleDate;
    EXPECT_TRUE(hasField(validate(draft), "date"));

    draft = validSession();
    draft.startTime = minutes{1440};
    EXPECT_TRUE(hasField(validate(draft), "start_time"));

    const auto durationHasError = [](int duration) {
        TrainingSessionDraft session = validSession();
        session.durationMin = duration;
        return hasField(validate(session), "duration_min");
    };
    EXPECT_TRUE(durationHasError(14));
    EXPECT_FALSE(durationHasError(15));
    EXPECT_FALSE(durationHasError(240));
    EXPECT_TRUE(durationHasError(241));
}

TEST(ValidationTest, AttendanceRangesHoldWhenPresent) {
    AttendanceDraft draft;
    draft.status = AttendanceStatus::Present;
    EXPECT_TRUE(validate(draft).empty());

    for (const int rpe : {0, 10}) {
        draft.rpe = rpe;
        EXPECT_FALSE(hasField(validate(draft), "rpe")) << rpe;
    }
    for (const int rpe : {-1, 11}) {
        draft.rpe = rpe;
        EXPECT_TRUE(hasField(validate(draft), "rpe")) << rpe;
    }
    draft.rpe = std::nullopt;
    for (const int value : {0, 240}) {
        draft.minutesOverride = value;
        EXPECT_FALSE(hasField(validate(draft), "minutes_override")) << value;
    }
    for (const int value : {-1, 241}) {
        draft.minutesOverride = value;
        EXPECT_TRUE(hasField(validate(draft), "minutes_override")) << value;
    }
}

TEST(ValidationTest, RpeAndMinutesAreEmptyUnlessPresent) {
    for (const AttendanceStatus status :
         {AttendanceStatus::Absent, AttendanceStatus::Excused, AttendanceStatus::Injured}) {
        SCOPED_TRACE(toDbText(status));
        AttendanceDraft draft;
        draft.status = status;
        EXPECT_TRUE(validate(draft).empty());

        draft.rpe = 5;
        EXPECT_TRUE(hasField(validate(draft), "rpe"));

        draft.rpe = std::nullopt;
        draft.minutesOverride = 30;
        EXPECT_TRUE(hasField(validate(draft), "minutes_override"));
    }
}

namespace {

MetricDefinitionDraft sprint() {
    MetricDefinitionDraft draft;
    draft.nameEn = "Sprint 30 m";
    draft.unit = "s";
    draft.valueType = MetricValueType::Decimal;
    draft.higherIsBetter = false;
    return draft;
}

} // namespace

TEST(ValidationTest, MetricNeedsOneNameOfAtMostFiftyCharacters) {
    MetricDefinitionDraft draft = sprint();
    EXPECT_TRUE(validate(draft).empty());

    draft.nameEn = "  ";
    EXPECT_TRUE(hasField(validate(draft), "name"));

    draft.namePl = polishName(50);
    EXPECT_TRUE(validate(draft).empty()) << "the Polish name alone is enough";

    draft.namePl = polishName(51);
    EXPECT_TRUE(hasField(validate(draft), "name_pl"));

    draft.namePl = "Sprint";
    draft.nameEn = std::string(51, 'a');
    EXPECT_TRUE(hasField(validate(draft), "name_en"));
}

TEST(ValidationTest, MetricNameMustGiveAKey) {
    MetricDefinitionDraft draft = sprint();
    draft.nameEn = "???";
    EXPECT_TRUE(hasField(validate(draft), "key"));
    EXPECT_FALSE(hasField(validate(draft), "name"));
}

TEST(ValidationTest, MetricUnitHasAtMostTwentyCharacters) {
    MetricDefinitionDraft draft = sprint();
    draft.unit = polishName(20);
    EXPECT_TRUE(validate(draft).empty());
    draft.unit = polishName(21);
    EXPECT_TRUE(hasField(validate(draft), "unit"));
}

TEST(ValidationTest, MetricLimitsFollowTheValueType) {
    MetricDefinitionDraft draft = sprint();
    draft.min = 2.5;
    draft.max = 9.75;
    EXPECT_TRUE(validate(draft).empty());

    draft.max = 2.0;
    EXPECT_TRUE(hasField(validate(draft), "max")) << "max below min";

    draft.max = 2.5;
    EXPECT_TRUE(validate(draft).empty()) << "min equal to max is a fixed value";

    draft.valueType = MetricValueType::Integer;
    EXPECT_TRUE(hasField(validate(draft), "min")) << "integer limits are whole";
    draft.min = 0.0;
    draft.max = 300.0;
    EXPECT_TRUE(validate(draft).empty());

    draft.valueType = MetricValueType::Boolean;
    draft.aggregation = MetricAggregation::Sum;
    const ValidationErrors errors = validate(draft);
    EXPECT_TRUE(hasField(errors, "min"));
    EXPECT_TRUE(hasField(errors, "max"));
    draft.min.reset();
    draft.max.reset();
    EXPECT_TRUE(validate(draft).empty());

    draft.valueType = MetricValueType::Decimal;
    draft.min = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(hasField(validate(draft), "min"));
}

TEST(ValidationTest, BooleanMetricAllowsSumAvgAndLast) {
    MetricDefinitionDraft draft = sprint();
    draft.unit.reset();
    draft.valueType = MetricValueType::Boolean;
    for (const MetricAggregation aggregation : allValues<MetricAggregation>()) {
        draft.aggregation = aggregation;
        const bool allowed = aggregation == MetricAggregation::Sum ||
                             aggregation == MetricAggregation::Avg ||
                             aggregation == MetricAggregation::Last;
        EXPECT_EQ(hasField(validate(draft), "aggregation"), !allowed) << toDbText(aggregation);
    }
}

TEST(ValidationTest, MetricValueOfEveryType) {
    MetricDefinitionDraft draft = sprint();
    draft.min = 3.0;
    draft.max = 8.0;
    EXPECT_TRUE(validateValue(draft, 4.35).empty());
    EXPECT_TRUE(validateValue(draft, 3.0).empty()) << "min is included";
    EXPECT_TRUE(validateValue(draft, 8.0).empty()) << "max is included";
    EXPECT_TRUE(hasField(validateValue(draft, 2.99), "value_range"));
    EXPECT_TRUE(hasField(validateValue(draft, 8.01), "value_range"));
    EXPECT_TRUE(hasField(validateValue(draft, std::numeric_limits<double>::quiet_NaN()), "value"));

    draft.min.reset();
    EXPECT_TRUE(validateValue(draft, -100.0).empty()) << "no lower limit";

    draft.valueType = MetricValueType::Integer;
    EXPECT_TRUE(validateValue(draft, 7.0).empty());
    EXPECT_TRUE(hasField(validateValue(draft, 7.5), "value"));

    draft.valueType = MetricValueType::Boolean;
    draft.max.reset();
    EXPECT_TRUE(validateValue(draft, 0.0).empty());
    EXPECT_TRUE(validateValue(draft, 1.0).empty());
    EXPECT_TRUE(hasField(validateValue(draft, 2.0), "value"));
}
