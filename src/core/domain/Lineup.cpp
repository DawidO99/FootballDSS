#include "core/domain/Lineup.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <utility>

namespace fdss::core {

std::optional<LineupPlace> findPlayer(const Lineup& lineup, PlayerId player) noexcept {
    for (std::size_t i = 0; i < lineup.pitchSlots.size(); ++i) {
        if (lineup.pitchSlots[i].player == player) {
            return LineupPlace{LineupPlace::Kind::Slot, static_cast<int>(i)};
        }
    }
    for (std::size_t i = 0; i < lineup.bench.size(); ++i) {
        if (lineup.bench[i] == player) {
            return LineupPlace{LineupPlace::Kind::Bench, static_cast<int>(i)};
        }
    }
    return std::nullopt;
}

int startingCount(const Lineup& lineup) noexcept {
    return static_cast<int>(std::ranges::count_if(
        lineup.pitchSlots, [](const LineupSlot& slot) { return slot.player.has_value(); }));
}

bool isValidSlot(const Lineup& lineup, int slot) noexcept {
    return slot >= 0 && static_cast<std::size_t>(slot) < lineup.pitchSlots.size();
}

LineupEdit assignToSlot(Lineup& lineup, int slot, PlayerId player) {
    if (!isValidSlot(lineup, slot)) {
        return LineupEdit::InvalidSlot;
    }
    LineupSlot& target = lineup.pitchSlots[static_cast<std::size_t>(slot)];
    if (target.player == player) {
        return LineupEdit::Unchanged;
    }
    const std::optional<PlayerId> occupant = target.player;
    const std::optional<LineupPlace> from = findPlayer(lineup, player);
    target.player = player;
    if (!from) {
        return LineupEdit::Done;
    }
    if (from->kind == LineupPlace::Kind::Slot) {
        lineup.pitchSlots[static_cast<std::size_t>(from->index)].player = occupant;
        return LineupEdit::Done;
    }
    const auto benchPlace = lineup.bench.begin() + from->index;
    if (occupant) {
        *benchPlace = *occupant;
    } else {
        lineup.bench.erase(benchPlace);
    }
    return LineupEdit::Done;
}

LineupEdit addToBench(Lineup& lineup, PlayerId player) {
    const std::optional<LineupPlace> from = findPlayer(lineup, player);
    if (from && from->kind == LineupPlace::Kind::Bench) {
        return LineupEdit::Unchanged;
    }
    if (std::ssize(lineup.bench) >= kMaxBench) {
        return LineupEdit::BenchFull;
    }
    if (from) {
        lineup.pitchSlots[static_cast<std::size_t>(from->index)].player.reset();
    }
    lineup.bench.push_back(player);
    return LineupEdit::Done;
}

LineupEdit swapSlots(Lineup& lineup, int first, int second) {
    if (!isValidSlot(lineup, first) || !isValidSlot(lineup, second)) {
        return LineupEdit::InvalidSlot;
    }
    auto& a = lineup.pitchSlots[static_cast<std::size_t>(first)].player;
    auto& b = lineup.pitchSlots[static_cast<std::size_t>(second)].player;
    if (a == b) {
        return LineupEdit::Unchanged;
    }
    std::swap(a, b);
    return LineupEdit::Done;
}

LineupEdit moveSlotToBench(Lineup& lineup, int slot) {
    if (!isValidSlot(lineup, slot)) {
        return LineupEdit::InvalidSlot;
    }
    const std::optional<PlayerId> player = lineup.pitchSlots[static_cast<std::size_t>(slot)].player;
    if (!player) {
        return LineupEdit::Unchanged;
    }
    if (std::ssize(lineup.bench) >= kMaxBench) {
        return LineupEdit::BenchFull;
    }
    lineup.pitchSlots[static_cast<std::size_t>(slot)].player.reset();
    lineup.bench.push_back(*player);
    return LineupEdit::Done;
}

LineupEdit removePlayer(Lineup& lineup, PlayerId player) {
    bool found = false;
    for (LineupSlot& slot : lineup.pitchSlots) {
        if (slot.player == player) {
            slot.player.reset();
            found = true;
        }
    }
    const auto removed = std::ranges::remove(lineup.bench, player);
    found = found || !removed.empty();
    lineup.bench.erase(removed.begin(), removed.end());
    return found ? LineupEdit::Done : LineupEdit::Unchanged;
}

void clearPlayers(Lineup& lineup) {
    for (LineupSlot& slot : lineup.pitchSlots) {
        slot.player.reset();
    }
    lineup.bench.clear();
}

} // namespace fdss::core
