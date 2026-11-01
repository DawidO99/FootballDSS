#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <optional>
#include <span>
#include <string>

namespace fdss::core {

struct AvailabilityDraft {
    AvailabilityType type = AvailabilityType::Injury;
    Date fromDate{};
    std::optional<Date> expectedTo;
    std::optional<Date> actualTo; // the first available day
    // On update, nullopt keeps the stored text: roles without ViewAvailabilityDetails must not wipe
    // it. An empty text clears it.
    std::optional<std::string> description;

    friend bool operator==(const AvailabilityDraft&, const AvailabilityDraft&) = default;
};

struct Availability : AvailabilityDraft {
    AvailabilityId id;
    PlayerId playerId;

    friend bool operator==(const Availability&, const Availability&) = default;
};

// Derived from the availability entries, never stored.
enum class PlayerStatus { Available, Injured, Ill, Suspended, Absent };

// Whether the entry keeps the player out on that date. actualTo, when set, is the first available
// day. Without it an injury or illness lasts until someone marks the return, because expectedTo is
// only an estimate; a suspension or absence has a known end and lasts through expectedTo.
[[nodiscard]] bool isActiveOn(const Availability& entry, Date date) noexcept;

// `entries` belong to one player. Overlapping entries: injury > illness > suspension > absence.
[[nodiscard]] PlayerStatus playerStatusOn(std::span<const Availability> entries,
                                          Date date) noexcept;

} // namespace fdss::core
