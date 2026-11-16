#include "core/algorithms/Formations.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <ranges>
#include <utility>

namespace fdss::core {

namespace {

using enum Position;

constexpr std::array kFormations{
    Formation{"4-4-2", {Gk, Rb, Cb, Cb, Lb, Rm, Cm, Cm, Lm, St, St}},
    Formation{"4-3-3", {Gk, Rb, Cb, Cb, Lb, Cm, Dm, Cm, Rw, St, Lw}},
    Formation{"4-2-3-1", {Gk, Rb, Cb, Cb, Lb, Dm, Dm, Rm, Am, Lm, St}},
    Formation{"4-1-4-1", {Gk, Rb, Cb, Cb, Lb, Dm, Rm, Cm, Cm, Lm, St}},
    Formation{"3-5-2", {Gk, Cb, Cb, Cb, Rwb, Cm, Dm, Cm, Lwb, St, St}},
    Formation{"3-4-3", {Gk, Cb, Cb, Cb, Rwb, Cm, Cm, Lwb, Rw, St, Lw}},
    Formation{"5-3-2", {Gk, Rwb, Cb, Cb, Cb, Lwb, Cm, Cm, Cm, St, St}},
};

// Rows from the own goal: top, height and the y of the tokens.
struct Row {
    double top;
    double height;
    double tokenY;
};
constexpr Row kGoalRow{0.88, 0.12, 0.94};
constexpr Row kDefenceRow{0.72, 0.16, 0.80};
constexpr Row kDefensiveMidfieldRow{0.56, 0.16, 0.64};
constexpr Row kMidfieldRow{0.40, 0.16, 0.48};
constexpr Row kAttackingMidfieldRow{0.24, 0.16, 0.32};
constexpr Row kForwardRow{0.0, 0.24, 0.12};

// Columns of a row with three zones: a narrow left and right, a wide centre.
constexpr double kSideWidth = 0.25;
constexpr double kCentreLeft = kSideWidth;
constexpr double kCentreWidth = 1.0 - 2 * kSideWidth;
constexpr double kRightLeft = 1.0 - kSideWidth;

constexpr PitchZone wholeRow(Position position, Row row) {
    // Dropped anywhere in the row, but the tokens stay in the middle half.
    return {position, 0.0, row.top, 1.0, row.height, kCentreLeft, kCentreWidth, row.tokenY};
}
constexpr PitchZone left(Position position, Row row) {
    return {position, 0.0, row.top, kSideWidth, row.height, 0.0, kSideWidth, row.tokenY};
}
constexpr PitchZone centre(Position position, Row row) {
    return {position,   kCentreLeft, row.top,      kCentreWidth,
            row.height, kCentreLeft, kCentreWidth, row.tokenY};
}
constexpr PitchZone right(Position position, Row row) {
    return {position,   kRightLeft, row.top,    kSideWidth,
            row.height, kRightLeft, kSideWidth, row.tokenY};
}

constexpr std::array kZones{
    wholeRow(Gk, kGoalRow),
    left(Lb, kDefenceRow),
    centre(Cb, kDefenceRow),
    right(Rb, kDefenceRow),
    left(Lwb, kDefensiveMidfieldRow),
    centre(Dm, kDefensiveMidfieldRow),
    right(Rwb, kDefensiveMidfieldRow),
    left(Lm, kMidfieldRow),
    centre(Cm, kMidfieldRow),
    right(Rm, kMidfieldRow),
    left(Lw, kAttackingMidfieldRow),
    centre(Am, kAttackingMidfieldRow),
    right(Rw, kAttackingMidfieldRow),
    wholeRow(St, kForwardRow),
};
static_assert(kZones.size() == EnumValues<Position>::values.size());

constexpr int kRows = 6;

// A grid of tokens in a zone (Codex review of P12): a column is at least this wide, so a side zone
// holds one token in a row and a wide one three; further tokens go to rows this far apart, which at
// the smallest board (about 350 × 470 px at 1280 × 720) is more than a token with its name.
constexpr double kMinZoneColumnWidth = 0.15;
constexpr double kZoneRowStep = 0.14;
// The centre of a token stays this far from the goal lines.
constexpr double kTokenMargin = 0.04;

std::size_t zoneColumns(const PitchZone& zone) {
    return std::max<std::size_t>(1,
                                 static_cast<std::size_t>(zone.layoutWidth / kMinZoneColumnWidth));
}

// The far edge of the pitch belongs to the zone at it.
bool within(double value, double start, double size) noexcept {
    const double end = start + size;
    return value >= start && (value < end || (end >= 1.0 && value <= 1.0));
}

std::vector<Position> sortedPositions(std::span<const LineupSlot> pitchSlots) {
    std::vector<Position> positions;
    positions.reserve(pitchSlots.size());
    for (const LineupSlot& slot : pitchSlots) {
        positions.push_back(slot.position);
    }
    std::ranges::sort(positions);
    return positions;
}

} // namespace

std::span<const Formation> formations() noexcept { return kFormations; }

const Formation* findFormation(std::string_view name) noexcept {
    const auto found = std::ranges::find(kFormations, name, &Formation::name);
    return found == kFormations.end() ? nullptr : &*found;
}

std::span<const PitchZone> pitchZones() noexcept { return kZones; }

const PitchZone& pitchZone(Position position) noexcept {
    const auto found = std::ranges::find(kZones, position, &PitchZone::position);
    // Every position has a zone (static_assert above and the unit test).
    return found == kZones.end() ? kZones.front() : *found;
}

Position positionAt(double x, double y) noexcept {
    const double cx = std::clamp(x, 0.0, 1.0);
    const double cy = std::clamp(y, 0.0, 1.0);
    for (const PitchZone& zone : kZones) {
        if (within(cx, zone.left, zone.width) && within(cy, zone.top, zone.height)) {
            return zone.position;
        }
    }
    return Cm; // not reached: the zones cover the pitch (unit test)
}

int pitchRow(Position position) noexcept {
    switch (position) {
    case Gk:
        return 0;
    case Lb:
    case Cb:
    case Rb:
        return 1;
    case Lwb:
    case Dm:
    case Rwb:
        return 2;
    case Lm:
    case Cm:
    case Rm:
        return 3;
    case Lw:
    case Am:
    case Rw:
        return 4;
    case St:
        return 5;
    }
    return 3;
}

void layoutZone(Lineup& lineup, Position zone) {
    std::vector<std::size_t> members;
    for (std::size_t i = 0; i < lineup.pitchSlots.size(); ++i) {
        if (lineup.pitchSlots[i].position == zone) {
            members.push_back(i);
        }
    }
    const PitchZone& geometry = pitchZone(zone);
    const std::size_t columns = zoneColumns(geometry);
    const std::size_t rows = (members.size() + columns - 1) / columns;
    // One row: left to right. Several: top to bottom, then left to right, so a drop picks its row.
    std::ranges::stable_sort(members, [&](std::size_t a, std::size_t b) {
        const LineupSlot& left = lineup.pitchSlots[a];
        const LineupSlot& right = lineup.pitchSlots[b];
        if (rows > 1 && left.y != right.y) {
            return left.y < right.y;
        }
        return left.x < right.x;
    });
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t first = row * columns;
        const std::size_t count = std::min(columns, members.size() - first);
        const double y =
            geometry.tokenY +
            (static_cast<double>(row) - static_cast<double>(rows - 1) / 2.0) * kZoneRowStep;
        for (std::size_t i = 0; i < count; ++i) {
            LineupSlot& slot = lineup.pitchSlots[members[first + i]];
            slot.x = geometry.layoutLeft + geometry.layoutWidth *
                                               (2.0 * static_cast<double>(i) + 1.0) /
                                               (2.0 * static_cast<double>(count));
            slot.y = std::clamp(y, kTokenMargin, 1.0 - kTokenMargin);
        }
    }
}

void layoutAllZones(Lineup& lineup) {
    for (const PitchZone& zone : kZones) {
        layoutZone(lineup, zone.position);
    }
}

std::string rowsName(std::span<const LineupSlot> pitchSlots) {
    std::array<int, kRows> counts{};
    for (const LineupSlot& slot : pitchSlots) {
        ++counts[static_cast<std::size_t>(pitchRow(slot.position))];
    }
    std::string name;
    for (std::size_t row = 1; row < counts.size(); ++row) {
        if (counts[row] == 0) {
            continue;
        }
        if (!name.empty()) {
            name += '-';
        }
        name += std::format("{}", counts[row]);
    }
    return name;
}

const Formation* matchingFormation(std::span<const LineupSlot> pitchSlots) noexcept {
    if (pitchSlots.size() != kLineupSlots) {
        return nullptr;
    }
    const std::vector<Position> positions = sortedPositions(pitchSlots);
    for (const Formation& formation : kFormations) {
        auto expected =
            std::vector<Position>(formation.positions.begin(), formation.positions.end());
        std::ranges::sort(expected);
        if (expected == positions) {
            return &formation;
        }
    }
    return nullptr;
}

std::string formationName(std::span<const LineupSlot> pitchSlots) {
    if (const Formation* formation = matchingFormation(pitchSlots)) {
        return std::string(formation->name);
    }
    return rowsName(pitchSlots);
}

Lineup emptyLineup(const Formation& formation) {
    Lineup lineup;
    lineup.formation = std::string(formation.name);
    for (const Position position : formation.positions) {
        lineup.pitchSlots.push_back(LineupSlot{position, 0.5, 0.5, std::nullopt});
    }
    layoutAllZones(lineup);
    return lineup;
}

LineupEdit moveSlot(Lineup& lineup, int slot, Position zone, std::optional<double> x,
                    std::optional<double> y) {
    if (!isValidSlot(lineup, slot)) {
        return LineupEdit::InvalidSlot;
    }
    LineupSlot& moved = lineup.pitchSlots[static_cast<std::size_t>(slot)];
    const Position from = moved.position;
    if (zone == Gk && from != Gk &&
        std::ranges::any_of(lineup.pitchSlots,
                            [](const LineupSlot& other) { return other.position == Gk; })) {
        return LineupEdit::GoalkeeperTaken;
    }
    const Lineup before = lineup;
    moved.position = zone;
    if (x) {
        moved.x = std::clamp(*x, 0.0, 1.0);
    }
    if (y) {
        moved.y = std::clamp(*y, 0.0, 1.0);
    }
    if (from != zone) {
        layoutZone(lineup, from);
    }
    layoutZone(lineup, zone);
    lineup.formation = formationName(lineup.pitchSlots);
    return lineup == before ? LineupEdit::Unchanged : LineupEdit::Done;
}

FormationChange changeFormation(const Lineup& lineup, const Formation& to) {
    FormationChange change{emptyLineup(to), {}, {}};
    std::vector<bool> placed(lineup.pitchSlots.size(), false);
    for (const PitchZone& zone : kZones) {
        // Both sides from left to right: the new slots are laid out, the old ones may not be.
        std::vector<std::size_t> oldSlots;
        for (std::size_t i = 0; i < lineup.pitchSlots.size(); ++i) {
            if (lineup.pitchSlots[i].position == zone.position && lineup.pitchSlots[i].player) {
                oldSlots.push_back(i);
            }
        }
        std::ranges::stable_sort(oldSlots, [&](std::size_t a, std::size_t b) {
            return lineup.pitchSlots[a].x < lineup.pitchSlots[b].x;
        });
        std::vector<std::size_t> newSlots;
        for (std::size_t i = 0; i < change.lineup.pitchSlots.size(); ++i) {
            if (change.lineup.pitchSlots[i].position == zone.position) {
                newSlots.push_back(i);
            }
        }
        std::ranges::stable_sort(newSlots, [&](std::size_t a, std::size_t b) {
            return change.lineup.pitchSlots[a].x < change.lineup.pitchSlots[b].x;
        });
        for (std::size_t i = 0; i < std::min(oldSlots.size(), newSlots.size()); ++i) {
            change.lineup.pitchSlots[newSlots[i]].player = lineup.pitchSlots[oldSlots[i]].player;
            placed[oldSlots[i]] = true;
        }
    }
    change.lineup.bench = lineup.bench;
    for (std::size_t i = 0; i < lineup.pitchSlots.size(); ++i) {
        const std::optional<PlayerId>& player = lineup.pitchSlots[i].player;
        if (!player || placed[i]) {
            continue;
        }
        if (std::ssize(change.lineup.bench) < kMaxBench) {
            change.lineup.bench.push_back(*player);
            change.movedToBench.push_back(*player);
        } else {
            change.leftLineup.push_back(*player);
        }
    }
    return change;
}

} // namespace fdss::core
