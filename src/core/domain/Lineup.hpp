#pragma once

#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <optional>
#include <string>
#include <vector>

// The lineup of a match or of a template (SPEC section 6, `lineup` and `.tactic.json`; packet P12).
// Coordinates: x 0 = the left touchline, 1 = the right one, seen from the own goal; y 0 = the goal
// line of the opponent, 1 = the own goal line.

namespace fdss::core {

inline constexpr int kLineupSlots = 11;
inline constexpr int kMaxBench = 12;

struct LineupSlot {
    Position position = Position::Cm;
    double x = 0.5;
    double y = 0.5;
    std::optional<PlayerId> player; // empty: a free slot

    friend bool operator==(const LineupSlot&, const LineupSlot&) = default;
};

struct Lineup {
    // The name of the shape, see formationName() in core/algorithms/Formations.hpp.
    std::string formation;
    std::vector<LineupSlot> pitchSlots; // kLineupSlots
    std::vector<PlayerId> bench;        // at most kMaxBench, in order

    friend bool operator==(const Lineup&, const Lineup&) = default;
};

// What an edit did. Anything but Done leaves the lineup as it was.
enum class LineupEdit { Done, Unchanged, BenchFull, GoalkeeperTaken, InvalidSlot };

struct LineupPlace {
    enum class Kind { Slot, Bench };
    Kind kind = Kind::Slot;
    int index = 0;

    friend bool operator==(const LineupPlace&, const LineupPlace&) = default;
};

// The first place of the player: the slots in order, then the bench.
[[nodiscard]] std::optional<LineupPlace> findPlayer(const Lineup& lineup, PlayerId player) noexcept;
// Slots with a player.
[[nodiscard]] int startingCount(const Lineup& lineup) noexcept;
[[nodiscard]] bool isValidSlot(const Lineup& lineup, int slot) noexcept;

// Puts the player in the slot. The player who was there takes the place the new one came from: the
// other slot (a swap) or the place on the bench; a player who came from outside the lineup pushes
// them out of it.
LineupEdit assignToSlot(Lineup& lineup, int slot, PlayerId player);
// At the end of the bench; a player from a slot leaves the slot free. A full bench: BenchFull.
LineupEdit addToBench(Lineup& lineup, PlayerId player);
// Swaps the players, never the positions.
LineupEdit swapSlots(Lineup& lineup, int first, int second);
LineupEdit moveSlotToBench(Lineup& lineup, int slot);
// Every place of the player.
LineupEdit removePlayer(Lineup& lineup, PlayerId player);
// The shape stays.
void clearPlayers(Lineup& lineup);

} // namespace fdss::core
