#include "core/domain/Availability.hpp"

#include <algorithm>

namespace fdss::core {

namespace {

bool hasKnownEnd(AvailabilityType type) noexcept {
    switch (type) {
    case AvailabilityType::Suspension:
    case AvailabilityType::Absence:
        return true;
    case AvailabilityType::Injury:
    case AvailabilityType::Illness:
        return false;
    }
    return false;
}

PlayerStatus statusFor(AvailabilityType type) noexcept {
    switch (type) {
    case AvailabilityType::Injury:
        return PlayerStatus::Injured;
    case AvailabilityType::Illness:
        return PlayerStatus::Ill;
    case AvailabilityType::Suspension:
        return PlayerStatus::Suspended;
    case AvailabilityType::Absence:
        return PlayerStatus::Absent;
    }
    return PlayerStatus::Available;
}

} // namespace

bool isActiveOn(const Availability& entry, Date date) noexcept {
    if (date < entry.fromDate) {
        return false;
    }
    if (entry.actualTo) {
        return date < *entry.actualTo;
    }
    if (entry.expectedTo && hasKnownEnd(entry.type)) {
        return date <= *entry.expectedTo;
    }
    return true;
}

PlayerStatus playerStatusOn(std::span<const Availability> entries, Date date) noexcept {
    // The enumeration lists the types from most to least important.
    for (const AvailabilityType type : allValues<AvailabilityType>()) {
        const bool isActive = std::ranges::any_of(entries, [&](const Availability& entry) {
            return entry.type == type && isActiveOn(entry, date);
        });
        if (isActive) {
            return statusFor(type);
        }
    }
    return PlayerStatus::Available;
}

} // namespace fdss::core
