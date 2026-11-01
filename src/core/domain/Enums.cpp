#include "core/domain/Enums.hpp"

#include <format>
#include <ranges>

// No default in the switches: the compiler reports a value added to an enum but not here.

namespace fdss::core {

std::string_view toDbText(TeamCategory value) noexcept {
    switch (value) {
    case TeamCategory::Senior:
        return "senior";
    case TeamCategory::U19:
        return "u19";
    case TeamCategory::U17:
        return "u17";
    case TeamCategory::U15:
        return "u15";
    case TeamCategory::U13:
        return "u13";
    case TeamCategory::U11:
        return "u11";
    case TeamCategory::Other:
        return "other";
    }
    return {};
}

std::string_view toDbText(Role value) noexcept {
    switch (value) {
    case Role::Coach:
        return "coach";
    case Role::Assistant:
        return "assistant";
    case Role::Physio:
        return "physio";
    case Role::Analyst:
        return "analyst";
    }
    return {};
}

std::string_view toDbText(Position value) noexcept {
    switch (value) {
    case Position::Gk:
        return "gk";
    case Position::Rb:
        return "rb";
    case Position::Cb:
        return "cb";
    case Position::Lb:
        return "lb";
    case Position::Rwb:
        return "rwb";
    case Position::Lwb:
        return "lwb";
    case Position::Dm:
        return "dm";
    case Position::Cm:
        return "cm";
    case Position::Am:
        return "am";
    case Position::Rm:
        return "rm";
    case Position::Lm:
        return "lm";
    case Position::Rw:
        return "rw";
    case Position::Lw:
        return "lw";
    case Position::St:
        return "st";
    }
    return {};
}

std::string_view toDbText(PreferredFoot value) noexcept {
    switch (value) {
    case PreferredFoot::Left:
        return "left";
    case PreferredFoot::Right:
        return "right";
    case PreferredFoot::Both:
        return "both";
    case PreferredFoot::Unknown:
        return "unknown";
    }
    return {};
}

std::string_view toDbText(AvailabilityType value) noexcept {
    switch (value) {
    case AvailabilityType::Injury:
        return "injury";
    case AvailabilityType::Illness:
        return "illness";
    case AvailabilityType::Suspension:
        return "suspension";
    case AvailabilityType::Absence:
        return "absence";
    }
    return {};
}

std::string_view toDbText(MatchStatus value) noexcept {
    switch (value) {
    case MatchStatus::Planned:
        return "planned";
    case MatchStatus::Live:
        return "live";
    case MatchStatus::Finished:
        return "finished";
    }
    return {};
}

std::string_view toDbText(Venue value) noexcept {
    switch (value) {
    case Venue::Home:
        return "home";
    case Venue::Away:
        return "away";
    case Venue::Neutral:
        return "neutral";
    }
    return {};
}

std::string_view toDbText(SessionType value) noexcept {
    switch (value) {
    case SessionType::Training:
        return "training";
    case SessionType::MatchPrep:
        return "match_prep";
    case SessionType::Recovery:
        return "recovery";
    case SessionType::Gym:
        return "gym";
    case SessionType::Other:
        return "other";
    }
    return {};
}

std::string_view toDbText(AttendanceStatus value) noexcept {
    switch (value) {
    case AttendanceStatus::Present:
        return "present";
    case AttendanceStatus::Absent:
        return "absent";
    case AttendanceStatus::Excused:
        return "excused";
    case AttendanceStatus::Injured:
        return "injured";
    }
    return {};
}

std::string_view toDbText(MatchEventType value) noexcept {
    switch (value) {
    case MatchEventType::Goal:
        return "goal";
    case MatchEventType::Assist:
        return "assist";
    case MatchEventType::Yellow:
        return "yellow";
    case MatchEventType::Red:
        return "red";
    case MatchEventType::Sub:
        return "sub";
    case MatchEventType::OpponentGoal:
        return "opponent_goal";
    }
    return {};
}

std::string_view toDbText(MetricValueType value) noexcept {
    switch (value) {
    case MetricValueType::Integer:
        return "integer";
    case MetricValueType::Decimal:
        return "decimal";
    case MetricValueType::Boolean:
        return "boolean";
    }
    return {};
}

std::string_view toDbText(MetricAggregation value) noexcept {
    switch (value) {
    case MetricAggregation::Sum:
        return "sum";
    case MetricAggregation::Avg:
        return "avg";
    case MetricAggregation::Min:
        return "min";
    case MetricAggregation::Max:
        return "max";
    case MetricAggregation::Last:
        return "last";
    }
    return {};
}

std::string_view toDbText(MetricScope value) noexcept {
    switch (value) {
    case MetricScope::Match:
        return "match";
    case MetricScope::Training:
        return "training";
    case MetricScope::Player:
        return "player";
    }
    return {};
}

Error unknownDbText(std::string_view text) {
    return Error{ErrorCode::InvalidInput, std::format("unknown value '{}'", text), {}};
}

PositionGroup positionGroup(Position position) noexcept {
    switch (position) {
    case Position::Gk:
        return PositionGroup::Goalkeeper;
    case Position::Rb:
    case Position::Cb:
    case Position::Lb:
    case Position::Rwb:
    case Position::Lwb:
        return PositionGroup::Defender;
    case Position::Dm:
    case Position::Cm:
    case Position::Am:
    case Position::Rm:
    case Position::Lm:
        return PositionGroup::Midfielder;
    case Position::Rw:
    case Position::Lw:
    case Position::St:
        return PositionGroup::Forward;
    }
    return PositionGroup::Midfielder;
}

std::string positionsToDbText(std::span<const Position> positions) {
    std::string text;
    for (const Position position : positions) {
        if (!text.empty()) {
            text += ',';
        }
        text += toDbText(position);
    }
    return text;
}

Result<std::vector<Position>> positionsFromDbText(std::string_view text) {
    std::vector<Position> positions;
    if (text.empty()) {
        return positions;
    }
    for (const auto token : text | std::views::split(',')) {
        const auto position = fromDbText<Position>(std::string_view(token.begin(), token.end()));
        if (!position) {
            return std::unexpected(position.error());
        }
        positions.push_back(*position);
    }
    return positions;
}

} // namespace fdss::core
