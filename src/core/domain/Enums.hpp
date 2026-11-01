#pragma once

#include "core/util/Error.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The enumerations of SPEC section 6. Text <-> enum mapping happens only here.

namespace fdss::core {

enum class TeamCategory { Senior, U19, U17, U15, U13, U11, Other };
enum class Role { Coach, Assistant, Physio, Analyst };
enum class Position { Gk, Rb, Cb, Lb, Rwb, Lwb, Dm, Cm, Am, Rm, Lm, Rw, Lw, St };
enum class PositionGroup { Goalkeeper, Defender, Midfielder, Forward };
enum class PreferredFoot { Left, Right, Both, Unknown };
enum class AvailabilityType { Injury, Illness, Suspension, Absence };
enum class MatchStatus { Planned, Live, Finished };
enum class Venue { Home, Away, Neutral };
enum class SessionType { Training, MatchPrep, Recovery, Gym, Other };
enum class AttendanceStatus { Present, Absent, Excused, Injured };
enum class MatchEventType { Goal, Assist, Yellow, Red, Sub, OpponentGoal };
enum class MetricValueType { Integer, Decimal, Boolean };
enum class MetricAggregation { Sum, Avg, Min, Max, Last };
enum class MetricScope { Match, Training, Player };

[[nodiscard]] std::string_view toDbText(TeamCategory value) noexcept;
[[nodiscard]] std::string_view toDbText(Role value) noexcept;
[[nodiscard]] std::string_view toDbText(Position value) noexcept;
[[nodiscard]] std::string_view toDbText(PreferredFoot value) noexcept;
[[nodiscard]] std::string_view toDbText(AvailabilityType value) noexcept;
[[nodiscard]] std::string_view toDbText(MatchStatus value) noexcept;
[[nodiscard]] std::string_view toDbText(Venue value) noexcept;
[[nodiscard]] std::string_view toDbText(SessionType value) noexcept;
[[nodiscard]] std::string_view toDbText(AttendanceStatus value) noexcept;
[[nodiscard]] std::string_view toDbText(MatchEventType value) noexcept;
[[nodiscard]] std::string_view toDbText(MetricValueType value) noexcept;
[[nodiscard]] std::string_view toDbText(MetricAggregation value) noexcept;
[[nodiscard]] std::string_view toDbText(MetricScope value) noexcept;

// Every value of an enumeration.
template <class E> struct EnumValues;

template <> struct EnumValues<TeamCategory> {
    static constexpr std::array values{TeamCategory::Senior, TeamCategory::U19, TeamCategory::U17,
                                       TeamCategory::U15,    TeamCategory::U13, TeamCategory::U11,
                                       TeamCategory::Other};
};
template <> struct EnumValues<Role> {
    static constexpr std::array values{Role::Coach, Role::Assistant, Role::Physio, Role::Analyst};
};
template <> struct EnumValues<Position> {
    static constexpr std::array values{Position::Gk,  Position::Rb,  Position::Cb, Position::Lb,
                                       Position::Rwb, Position::Lwb, Position::Dm, Position::Cm,
                                       Position::Am,  Position::Rm,  Position::Lm, Position::Rw,
                                       Position::Lw,  Position::St};
};
template <> struct EnumValues<PreferredFoot> {
    static constexpr std::array values{PreferredFoot::Left, PreferredFoot::Right,
                                       PreferredFoot::Both, PreferredFoot::Unknown};
};
template <> struct EnumValues<AvailabilityType> {
    static constexpr std::array values{AvailabilityType::Injury, AvailabilityType::Illness,
                                       AvailabilityType::Suspension, AvailabilityType::Absence};
};
template <> struct EnumValues<MatchStatus> {
    static constexpr std::array values{MatchStatus::Planned, MatchStatus::Live,
                                       MatchStatus::Finished};
};
template <> struct EnumValues<Venue> {
    static constexpr std::array values{Venue::Home, Venue::Away, Venue::Neutral};
};
template <> struct EnumValues<SessionType> {
    static constexpr std::array values{SessionType::Training, SessionType::MatchPrep,
                                       SessionType::Recovery, SessionType::Gym, SessionType::Other};
};
template <> struct EnumValues<AttendanceStatus> {
    static constexpr std::array values{AttendanceStatus::Present, AttendanceStatus::Absent,
                                       AttendanceStatus::Excused, AttendanceStatus::Injured};
};
template <> struct EnumValues<MatchEventType> {
    static constexpr std::array values{MatchEventType::Goal,   MatchEventType::Assist,
                                       MatchEventType::Yellow, MatchEventType::Red,
                                       MatchEventType::Sub,    MatchEventType::OpponentGoal};
};
template <> struct EnumValues<MetricValueType> {
    static constexpr std::array values{MetricValueType::Integer, MetricValueType::Decimal,
                                       MetricValueType::Boolean};
};
template <> struct EnumValues<MetricAggregation> {
    static constexpr std::array values{MetricAggregation::Sum, MetricAggregation::Avg,
                                       MetricAggregation::Min, MetricAggregation::Max,
                                       MetricAggregation::Last};
};
template <> struct EnumValues<MetricScope> {
    static constexpr std::array values{MetricScope::Match, MetricScope::Training,
                                       MetricScope::Player};
};

template <class E> [[nodiscard]] constexpr std::span<const E> allValues() noexcept {
    return EnumValues<E>::values;
}

// The InvalidInput error of fromDbText(). Not a template, so the header needs no <format>.
[[nodiscard]] Error unknownDbText(std::string_view text);

template <class E> [[nodiscard]] Result<E> fromDbText(std::string_view text) {
    for (const E value : allValues<E>()) {
        if (toDbText(value) == text) {
            return value;
        }
    }
    return std::unexpected(unknownDbText(text));
}

[[nodiscard]] PositionGroup positionGroup(Position position) noexcept;

// player.secondary_positions: a comma list such as "cb,rb", empty for no positions.
[[nodiscard]] std::string positionsToDbText(std::span<const Position> positions);
[[nodiscard]] Result<std::vector<Position>> positionsFromDbText(std::string_view text);

} // namespace fdss::core
