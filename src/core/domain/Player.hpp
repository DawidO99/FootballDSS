#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fdss::core {

struct PlayerDraft {
    std::string firstName;
    std::string lastName;
    std::optional<int> shirtNumber;
    std::optional<int> birthYear;
    Position primaryPosition = Position::Cm;
    std::vector<Position> secondaryPositions;
    PreferredFoot foot = PreferredFoot::Unknown;
    std::optional<std::string> notes;

    friend bool operator==(const PlayerDraft&, const PlayerDraft&) = default;
};

struct Player : PlayerDraft {
    PlayerId id;
    TeamId teamId;
    std::optional<Date> archivedAt;

    friend bool operator==(const Player&, const Player&) = default;
};

} // namespace fdss::core
