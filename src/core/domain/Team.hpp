#pragma once

#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <string>

namespace fdss::core {

struct TeamDraft {
    std::string name;
    TeamCategory category = TeamCategory::Senior;
    std::string season; // "2026/27"

    friend bool operator==(const TeamDraft&, const TeamDraft&) = default;
};

struct Team : TeamDraft {
    TeamId id;
    ClubId clubId;

    friend bool operator==(const Team&, const Team&) = default;
};

} // namespace fdss::core
