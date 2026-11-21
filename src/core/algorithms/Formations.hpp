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

// Lays the tokens of the zone out in a grid: as many in a row as the zone has room for (one in a
// side zone, three in a wide one), left to right by x; more tokens go to further rows around the y
// of the zone, top to bottom by y. Tokens of one zone never overlap.
// `occupiedOnly` (live tactics, P13b): empty slots (a player sent off) take no room and keep their
// coordinates.
void layoutZone(Lineup& lineup, Position zone, bool occupiedOnly = false);
void layoutAllZones(Lineup& lineup, bool occupiedOnly = false);

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
// and `y` are where in the new zone (a drop), which decide its place in the grid; without them the
// slot keeps its coordinates for the order. A second goalkeeper: GoalkeeperTaken. The formation
// name follows.
LineupEdit moveSlot(Lineup& lineup, int slot, Position zone, std::optional<double> x = std::nullopt,
                    std::optional<double> y = std::nullopt);

struct FormationChange {
    Lineup lineup;
    std::vector<PlayerId> movedToBench;
    std::vector<PlayerId> leftLineup; // the bench was full
};

// The new formation, laid out. Each of its positions takes the players of the same position from
// the old slots, from left to right; the others go to the end of the bench while it has room and
// leave the lineup after that. The bench stays as it was before them.
[[nodiscard]] FormationChange changeFormation(const Lineup& lineup, const Formation& to);

// --- Live tactics (packet P13b). The shape of a running match: the 11 slots of the saved lineup
// keep their index, players stay in their slots (a substitute takes the slot of the player going
// off), an empty slot is a player sent off. Edits change the places of the slots (position and
// coordinates), never who is in them, and lay out the zones with the occupied slots only. An empty
// slot never blocks a position.

// The name of the occupied slots: formationName() with eleven, otherwise rowsName() ("4-4-1").
[[nodiscard]] std::string liveFormationName(const Lineup& shape);
// The occupied slots are eleven and their positions are not a catalogue formation.
[[nodiscard]] bool isCustomLiveShape(const Lineup& shape);
// Whether an occupied slot holds the GK position.
[[nodiscard]] bool hasGoalkeeperOnPitch(const Lineup& shape) noexcept;

// The formation for a live match: nobody leaves the pitch. With fewer than eleven players the
// empty slots take the positions of `to` left over: the goalkeeper when none is on the pitch, then
// the ones furthest forward (ten players in 4-4-2 play 4-4-1). Each remaining position first takes
// the occupied slots of the same position, left to right (as changeFormation()); the other occupied
// slots take the free positions from the own goal up, left to right (the GK position only when
// nothing else is left). Laid out.
[[nodiscard]] Lineup changeShape(const Lineup& shape, const Formation& to);
// Exchanges the places (position and coordinates) of two occupied slots, so on the board their
// players swap places. The same slot: Unchanged; an empty or invalid one: InvalidSlot.
LineupEdit swapPlaces(Lineup& shape, int first, int second);
// moveSlot() for a live shape: the slot must be occupied (InvalidSlot); the GK zone is refused only
// when an occupied slot holds it (GoalkeeperTaken); an empty slot there gives its place to the slot
// that moves in.
LineupEdit moveLiveSlot(Lineup& shape, int slot, Position zone,
                        std::optional<double> x = std::nullopt,
                        std::optional<double> y = std::nullopt);

} // namespace fdss::core
