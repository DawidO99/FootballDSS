#pragma once

#include <array>
#include <optional>
#include <vector>

// The adaptive capability model (SPEC section 2, packet P15): which features the data in the
// database supports. Pure: callers count the data and pass the thresholds in. The result carries
// numbers for the hint ("9 more days"), never text.

namespace fdss::core {

enum class CapabilityKind { Squad, Matches, Load, Ratings, Opponents, EventData };

inline constexpr std::array kCapabilityKinds{CapabilityKind::Squad,     CapabilityKind::Matches,
                                             CapabilityKind::Load,      CapabilityKind::Ratings,
                                             CapabilityKind::Opponents, CapabilityKind::EventData};

enum class CapabilityState {
    Available,
    Locked, // visible, with a hint of what is missing
    Hidden  // a Pro module without data while "Show advanced modules" is off
};

// The defaults of SPEC section 2.
struct CapabilityThresholds {
    int minPlayers = 11;          // active players
    int minFinishedMatches = 1;   // finished matches
    int minRpeDays = 14;          // days with at least one RPE
    int fullLoadDays = 28;        // days since the first RPE before the full acute:chronic ratio
    int minRatingEntries = 1;     // match ratings, player attributes or custom metric values
    int minMatchesWithResult = 3; // finished matches with both goals stored
    int minEventFiles = 1;        // imported event data files

    friend bool operator==(const CapabilityThresholds&, const CapabilityThresholds&) = default;
};

// What the database holds for one team.
struct DataCounts {
    int activePlayers = 0;
    int finishedMatches = 0;
    int rpeDays = 0;
    // Through today, the first RPE day counting as day 1; 0 without any RPE.
    int daysSinceFirstRpe = 0;
    int ratingEntries = 0;
    int matchesWithResult = 0;
    int eventFiles = 0;

    friend bool operator==(const DataCounts&, const DataCounts&) = default;
};

struct Capability {
    CapabilityKind kind = CapabilityKind::Squad;
    CapabilityState state = CapabilityState::Locked;
    int have = 0; // the counter the condition looks at
    int need = 0; // its threshold
    // Load only, while available: days until the full acute:chronic ratio; nullopt once it is
    // there.
    std::optional<int> daysToFullLoad;

    // How many are still missing; 0 when the condition holds.
    [[nodiscard]] int missing() const noexcept { return have >= need ? 0 : need - have; }

    friend bool operator==(const Capability&, const Capability&) = default;
};

// One entry per kind, in the order of kCapabilityKinds. Event data without any file is hidden
// unless showAdvanced (SPEC section 2: Pro modules stay hidden until data exists).
[[nodiscard]] std::vector<Capability> evaluateCapabilities(const DataCounts& counts,
                                                           const CapabilityThresholds& thresholds,
                                                           bool showAdvanced);

} // namespace fdss::core
