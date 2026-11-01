#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <optional>
#include <string>

namespace fdss::core {

struct MatchDraft {
    OpponentId opponentId;
    Date date{};
    std::optional<TimeOfDay> kickoffTime;
    Venue venue = Venue::Home;
    std::optional<std::string> competition;

    friend bool operator==(const MatchDraft&, const MatchDraft&) = default;
};

struct Match : MatchDraft {
    MatchId id;
    TeamId teamId;
    std::optional<int> goalsFor;
    std::optional<int> goalsAgainst;
    MatchStatus status = MatchStatus::Planned;
    std::optional<std::string> formation;

    friend bool operator==(const Match&, const Match&) = default;
};

} // namespace fdss::core
