#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fdss::core {

struct StaffUserDraft {
    std::string name;
    Role role = Role::Analyst;
    bool active = true;

    friend bool operator==(const StaffUserDraft&, const StaffUserDraft&) = default;
};

// Holds no PIN data, so listing the staff never loads it.
struct StaffUser : StaffUserDraft {
    StaffUserId id;

    friend bool operator==(const StaffUser&, const StaffUser&) = default;
};

// ADR-007. Hashing itself lives in the services.
struct PinHash {
    std::vector<std::uint8_t> hash;
    std::vector<std::uint8_t> salt;
    int iterations = 0;

    friend bool operator==(const PinHash&, const PinHash&) = default;
};

struct StaffCredentials : PinHash {
    int failedAttempts = 0;
    std::optional<Timestamp> lockedUntil;
    // A Coach chose the PIN; the user sets their own at the next login (P19).
    bool mustChangePin = false;

    friend bool operator==(const StaffCredentials&, const StaffCredentials&) = default;
};

} // namespace fdss::core
