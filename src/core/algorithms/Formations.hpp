#pragma once

#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The formation catalogue and the position zones of the tactics board (packet P12, decision 12).
// Coordinates as in core/domain/Lineup.hpp: x 0 = left, 1 = right, seen from the own goal; y 0 =
// the goal line of the opponent, 1 = the own goal line.
//
// The pitch is split into one zone per position, in rows from the own goal: GK | LB CB RB |
// LWB DM RWB | LM CM RM | LW AM RW | ST. A slot takes the position of the zone it is dropped in,
// and the tokens of one zone are spread over it in their left-to-right order, so the coordinates of
// a catalogue formation come from the same layout.

namespace fdss::core {

struct Formation {
    std::string_view name;
    std::array<Position, kLineupSlots> positions; // the goalkeeper first
};

// The seven formations of the packet, in its order.
[[nodiscard]] std::span<const Formation> formations() noexcept;
// nullptr: not in the catalogue.
[[nodiscard]] const Formation* findFormation(std::string_view name) noexcept;

struct PitchZone {
    Position position = Position::Cm;
    // The rectangle a drop lands in.
    double left = 0.0;
    double top = 0.0;
    double width = 0.0;
    double height = 0.0;
    // Where the tokens of the zone are spread, and their y.
    double layoutLeft = 0.0;
    double layoutWidth = 0.0;
    double tokenY = 0.0;
};

// One zone per position, the rows from the own goal, each row from left to right.
[[nodiscard]] std::span<const PitchZone> pitchZones() noexcept;
[[nodiscard]] const PitchZone& pitchZone(Position position) noexcept;
// The zone that holds the point. Coordinates outside 0–1 count as the nearest edge.
[[nodiscard]] Position positionAt(double x, double y) noexcept;
// The row of the zone: 0 goalkeeper, 1 defence (LB CB RB), 2 LWB DM RWB, 3 LM CM RM, 4 LW AM RW, 5
// ST.
[[nodiscard]] int pitchRow(Position position) noexcept;

// Spreads the tokens of the zone over it in their left-to-right order (by x, then slot order) and
// gives them the y of the zone.
void layoutZone(Lineup& lineup, Position zone);
void layoutAllZones(Lineup& lineup);

// The outfield rows from defence to attack with the number of slots in them, empty rows left out,
// e.g. "4-2-3-1". The goalkeeper is not counted.
[[nodiscard]] std::string rowsName(std::span<const LineupSlot> pitchSlots);
// The formation whose positions are exactly those of the slots, in any order; nullptr: a custom
// shape.
[[nodiscard]] const Formation* matchingFormation(std::span<const LineupSlot> pitchSlots) noexcept;
// The catalogue name of a catalogue shape, otherwise rowsName(). The UI marks a custom shape.
[[nodiscard]] std::string formationName(std::span<const LineupSlot> pitchSlots);

// The catalogue formation without players, laid out.
[[nodiscard]] Lineup emptyLineup(const Formation& formation);

// Gives the slot the position of `zone` and lays out the zone it left and the one it entered. `x`
// is where in the new zone (a drop); without it the slot keeps its x for the order. A second
// goalkeeper: GoalkeeperTaken. The formation name follows.
LineupEdit moveSlot(Lineup& lineup, int slot, Position zone,
                    std::optional<double> x = std::nullopt);

struct FormationChange {
    Lineup lineup;
    std::vector<PlayerId> movedToBench;
    std::vector<PlayerId> leftLineup; // the bench was full
};

// The new formation, laid out. Each of its positions takes the players of the same position from
// the old slots, from left to right; the others go to the end of the bench while it has room and
// leave the lineup after that. The bench stays as it was before them.
[[nodiscard]] FormationChange changeFormation(const Lineup& lineup, const Formation& to);

} // namespace fdss::core
