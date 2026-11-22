#include "core/domain/Validation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace fdss::core {

namespace {

constexpr std::size_t kMaxPersonNameLength = 50;
constexpr std::size_t kMaxOrganisationNameLength = 100;
constexpr int kMaxMatchMinute = 130;
constexpr int kMinRating = 1;
constexpr int kMaxRating = 10;
constexpr std::chrono::minutes kMinutesPerDay{24 * 60};
constexpr std::string_view kAsciiWhitespace = " \t\n\v\f\r";

void add(ValidationErrors& errors, std::string_view field) {
    errors.push_back(ValidationError{std::string(field), ErrorCode::InvalidInput});
}

void checkName(ValidationErrors& errors, std::string_view field, std::string_view name,
               std::size_t maxLength) {
    const std::size_t length = characterCount(trimmed(name));
    if (length == 0 || length > maxLength) {
        add(errors, field);
    }
}

void checkRange(ValidationErrors& errors, std::string_view field, int value, int min, int max) {
    if (value < min || value > max) {
        add(errors, field);
    }
}

void checkRange(ValidationErrors& errors, std::string_view field, const std::optional<int>& value,
                int min, int max) {
    if (value) {
        checkRange(errors, field, *value, min, max);
    }
}

void checkTime(ValidationErrors& errors, std::string_view field,
               const std::optional<TimeOfDay>& time) {
    if (time && (*time < TimeOfDay::zero() || *time >= kMinutesPerDay)) {
        add(errors, field);
    }
}

void checkEndDate(ValidationErrors& errors, std::string_view field, const std::optional<Date>& end,
                  Date from) {
    // A broken start is reported on its own field; ordering against it would mean nothing.
    if (end && (!isSupportedDate(*end) || (isSupportedDate(from) && *end < from))) {
        add(errors, field);
    }
}

bool isDigit(char character) noexcept { return character >= '0' && character <= '9'; }

int digitsValue(std::string_view digits) noexcept {
    int value = 0;
    for (const char digit : digits) {
        value = value * 10 + (digit - '0');
    }
    return value;
}

// "2026/27": the format the schema checks, a first year from 1900 to 2099 and a second part that
// is the next year, so "2099/00" is the last season.
bool isSeason(std::string_view season) noexcept {
    constexpr std::size_t kSeasonLength = 7;
    constexpr std::size_t kSeparatorIndex = 4;
    constexpr int kFirstYear = 1900;
    constexpr int kLastYear = 2099;
    constexpr int kCentury = 100;
    if (season.size() != kSeasonLength) {
        return false;
    }
    for (std::size_t i = 0; i < season.size(); ++i) {
        if (i == kSeparatorIndex ? season[i] != '/' : !isDigit(season[i])) {
            return false;
        }
    }
    const int first = digitsValue(season.substr(0, kSeparatorIndex));
    const int second = digitsValue(season.substr(kSeparatorIndex + 1));
    return first >= kFirstYear && first <= kLastYear && second == (first + 1) % kCentury;
}

bool isWhole(double value) noexcept { return std::isfinite(value) && std::trunc(value) == value; }

void checkLimit(ValidationErrors& errors, std::string_view field,
                const std::optional<double>& limit, MetricValueType type) {
    if (!limit) {
        return;
    }
    if (type == MetricValueType::Boolean || !std::isfinite(*limit) ||
        (type == MetricValueType::Integer && !isWhole(*limit))) {
        add(errors, field);
    }
}

bool hasDuplicates(std::vector<Position> positions) {
    std::ranges::sort(positions);
    return std::ranges::adjacent_find(positions) != positions.end();
}

} // namespace

std::string trimmed(std::string_view utf8) {
    const std::size_t first = utf8.find_first_not_of(kAsciiWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = utf8.find_last_not_of(kAsciiWhitespace);
    return std::string(utf8.substr(first, last - first + 1));
}

std::size_t characterCount(std::string_view utf8) noexcept {
    // Continuation bytes look like 10xxxxxx.
    return static_cast<std::size_t>(std::ranges::count_if(
        utf8, [](char byte) { return (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U; }));
}

ValidationErrors validate(const ClubDraft& draft) {
    ValidationErrors errors;
    checkName(errors, "name", draft.name, kMaxOrganisationNameLength);
    return errors;
}

ValidationErrors validate(const TeamDraft& draft) {
    ValidationErrors errors;
    checkName(errors, "name", draft.name, kMaxOrganisationNameLength);
    if (!isSeason(draft.season)) {
        add(errors, "season");
    }
    return errors;
}

ValidationErrors validate(const StaffUserDraft& draft) {
    ValidationErrors errors;
    checkName(errors, "name", draft.name, kMaxPersonNameLength);
    return errors;
}

ValidationErrors validate(const PlayerDraft& draft, int currentYear) {
    ValidationErrors errors;
    checkName(errors, "first_name", draft.firstName, kMaxPersonNameLength);
    checkName(errors, "last_name", draft.lastName, kMaxPersonNameLength);
    checkRange(errors, "shirt_number", draft.shirtNumber, kMinShirtNumber, kMaxShirtNumber);
    checkRange(errors, "birth_year", draft.birthYear, kMinBirthYear,
               currentYear - kYoungestPlayerAge);
    if (hasDuplicates(draft.secondaryPositions) ||
        std::ranges::find(draft.secondaryPositions, draft.primaryPosition) !=
            draft.secondaryPositions.end()) {
        add(errors, "secondary_positions");
    }
    return errors;
}

ValidationErrors validate(const AvailabilityDraft& draft) {
    ValidationErrors errors;
    if (!isSupportedDate(draft.fromDate)) {
        add(errors, "from_date");
    }
    checkEndDate(errors, "expected_to", draft.expectedTo, draft.fromDate);
    checkEndDate(errors, "actual_to", draft.actualTo, draft.fromDate);
    return errors;
}

ValidationErrors validate(const OpponentDraft& draft) {
    ValidationErrors errors;
    checkName(errors, "name", draft.name, kMaxOrganisationNameLength);
    return errors;
}

ValidationErrors validate(const MatchDraft& draft) {
    ValidationErrors errors;
    if (!isSupportedDate(draft.date)) {
        add(errors, "date");
    }
    checkTime(errors, "kickoff_time", draft.kickoffTime);
    return errors;
}

ValidationErrors validate(const AppearanceDraft& draft) {
    ValidationErrors errors;
    checkRange(errors, "minute_on", draft.minuteOn, 0, kMaxMatchMinute);
    checkRange(errors, "minute_off", draft.minuteOff, 0, kMaxMatchMinute);
    if (draft.minuteOn && draft.minuteOff && *draft.minuteOff < *draft.minuteOn) {
        add(errors, "minute_off");
    }
    checkRange(errors, "rating", draft.rating, kMinRating, kMaxRating);
    checkRange(errors, "rpe", draft.rpe, 0, kMaxRpe);
    return errors;
}

ValidationErrors validate(const MatchEventDraft& draft) {
    ValidationErrors errors;
    checkRange(errors, "minute", draft.minute, 0, kMaxMatchMinute);
    if (!draft.playerId && draft.type != MatchEventType::OpponentGoal) {
        add(errors, "player_id");
    }
    return errors;
}

ValidationErrors validate(const TrainingSessionDraft& draft) {
    ValidationErrors errors;
    if (!isSupportedDate(draft.date)) {
        add(errors, "date");
    }
    checkTime(errors, "start_time", draft.startTime);
    checkRange(errors, "duration_min", draft.durationMin, kMinSessionDuration, kMaxSessionDuration);
    return errors;
}

ValidationErrors validate(const AttendanceDraft& draft) {
    ValidationErrors errors;
    const bool isPresent = draft.status == AttendanceStatus::Present;
    if (draft.rpe && !isPresent) {
        add(errors, "rpe");
    } else {
        checkRange(errors, "rpe", draft.rpe, 0, kMaxRpe);
    }
    if (draft.minutesOverride && !isPresent) {
        add(errors, "minutes_override");
    } else {
        checkRange(errors, "minutes_override", draft.minutesOverride, 0, kMaxAttendanceMinutes);
    }
    return errors;
}

ValidationErrors validate(const MetricDefinitionDraft& draft) {
    ValidationErrors errors;
    const std::size_t plLength = characterCount(trimmed(draft.namePl));
    const std::size_t enLength = characterCount(trimmed(draft.nameEn));
    if (plLength == 0 && enLength == 0) {
        add(errors, "name");
    }
    if (plLength > kMaxMetricNameLength) {
        add(errors, "name_pl");
    }
    if (enLength > kMaxMetricNameLength) {
        add(errors, "name_en");
    }
    if ((plLength > 0 || enLength > 0) && metricKey(draft).empty()) {
        add(errors, "key");
    }
    if (draft.unit && characterCount(trimmed(*draft.unit)) > kMaxMetricUnitLength) {
        add(errors, "unit");
    }
    checkLimit(errors, "min", draft.min, draft.valueType);
    checkLimit(errors, "max", draft.max, draft.valueType);
    if (draft.min && draft.max && *draft.max < *draft.min &&
        std::ranges::find(errors, std::string("max"), &ValidationError::field) == errors.end()) {
        add(errors, "max");
    }
    if (!isAllowed(draft.valueType, draft.aggregation)) {
        add(errors, "aggregation");
    }
    return errors;
}

ValidationErrors validateValue(const MetricDefinitionDraft& metric, double value) {
    ValidationErrors errors;
    const bool wrongKind =
        !std::isfinite(value) ||
        (metric.valueType == MetricValueType::Integer && !isWhole(value)) ||
        (metric.valueType == MetricValueType::Boolean && value != 0.0 && value != 1.0);
    if (wrongKind) {
        add(errors, "value");
    } else if ((metric.min && value < *metric.min) || (metric.max && value > *metric.max)) {
        add(errors, "value_range");
    }
    return errors;
}

} // namespace fdss::core
