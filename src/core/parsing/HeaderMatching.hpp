#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Header and value matching for imports (SPEC section 8 step 3): texts are compared after
// foldText(), so "Imię", "IMIE" and " imię: " are the same header, and "ŚO" equals "so".

namespace fdss::core {

// Lower case; Latin letters without diacritics (ł -> l, ß -> s); digits and '#' kept; every run of
// other characters one space; trimmed.
[[nodiscard]] std::string foldText(std::string_view utf8);

// Name matching: fold Latin case/diacritics and ASCII separators as above, preserving all
// other Unicode code points instead of discarding letters from other scripts.
[[nodiscard]] std::string foldName(std::string_view utf8);

// The synonyms of one field of an import type, already folded. `field` is the value of that
// type's own field enum, so every import type reuses the matching.
struct FieldSynonyms {
    int field = 0;
    std::span<const std::string_view> names;
};

// The field of every header, or nullopt. A field goes to the first header that names it.
[[nodiscard]] std::vector<std::optional<int>> matchHeaders(std::span<const std::string> headers,
                                                           std::span<const FieldSynonyms> fields);

// The folded headers joined with '|': the same header set gives the same key, whatever the case,
// the diacritics or the spacing.
[[nodiscard]] std::string headerKey(std::span<const std::string> headers);

} // namespace fdss::core
