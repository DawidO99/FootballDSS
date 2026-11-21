#pragma once

#include "core/domain/Enums.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Positions and the preferred foot as people write them in their files, Polish and English:
// codes (BR/GK, ŚO/CB, N/ST…) and names ("bramkarz", "centre back"…), any case, with or without
// diacritics.

namespace fdss::core {

[[nodiscard]] std::optional<Position> parsePosition(std::string_view text);

// A list separated by ',', ';', '/', '|' or '+'. Entries that are not a position go to `unknown`
// as written (trimmed); the order of `positions` is the order of the text, without duplicates.
struct PositionList {
    std::vector<Position> positions;
    std::vector<std::string> unknown;
};
[[nodiscard]] PositionList parsePositionList(std::string_view text);

// L/P/R/B, "lewa", "prawa", "obie", "left", "right", "both"… Empty: nullopt as well.
[[nodiscard]] std::optional<PreferredFoot> parseFoot(std::string_view text);

} // namespace fdss::core
