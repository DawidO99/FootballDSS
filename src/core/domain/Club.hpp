#pragma once

#include "core/domain/Ids.hpp"

#include <string>

namespace fdss::core {

// A draft is what the user edits; the entity adds what the database assigns.
struct ClubDraft {
    std::string name;

    friend bool operator==(const ClubDraft&, const ClubDraft&) = default;
};

struct Club : ClubDraft {
    ClubId id;

    friend bool operator==(const Club&, const Club&) = default;
};

} // namespace fdss::core
