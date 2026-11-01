#pragma once

#include "core/domain/Ids.hpp"

#include <optional>
#include <string>

namespace fdss::core {

// name_key stays in the data layer: case folding needs Qt.
struct OpponentDraft {
    std::string name;
    std::optional<std::string> notes;

    friend bool operator==(const OpponentDraft&, const OpponentDraft&) = default;
};

struct Opponent : OpponentDraft {
    OpponentId id;

    friend bool operator==(const Opponent&, const Opponent&) = default;
};

} // namespace fdss::core
