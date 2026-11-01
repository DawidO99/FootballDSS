#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"

#include <optional>
#include <string>

namespace fdss::core {

struct TrainingSessionDraft {
    Date date{};
    std::optional<TimeOfDay> startTime;
    int durationMin = 90;
    SessionType type = SessionType::Training;
    std::optional<std::string> note;

    friend bool operator==(const TrainingSessionDraft&, const TrainingSessionDraft&) = default;
};

struct TrainingSession : TrainingSessionDraft {
    TrainingSessionId id;
    TeamId teamId;

    friend bool operator==(const TrainingSession&, const TrainingSession&) = default;
};

} // namespace fdss::core
