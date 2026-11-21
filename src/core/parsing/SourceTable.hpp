#pragma once

#include "core/util/Error.hpp"

#include <string>
#include <vector>

// A file of an import after reading: a header and the rows below it, every cell as UTF-8 text.
// CSV and XLSX readers both produce SourceRow lists; the rest of the import does not know which.

namespace fdss::core {

struct SourceRow {
    int number = 0; // line of the CSV file or row of the sheet, 1-based, as the user sees it
    std::vector<std::string> cells;

    friend bool operator==(const SourceRow&, const SourceRow&) = default;
};

struct SourceTable {
    int headerRow = 0;
    std::vector<std::string> headers; // trimmed
    std::vector<SourceRow> rows;      // blank rows removed

    friend bool operator==(const SourceTable&, const SourceTable&) = default;
};

// Rows whose cells are all whitespace are left out. The first remaining row is the header (SPEC
// section 8 step 2); trailing empty header cells are dropped unless a row below has data there.
// No row with text: InvalidInput with field "import_empty". A header without rows: InvalidInput
// with field "import_no_rows".
[[nodiscard]] Result<SourceTable> toSourceTable(std::vector<SourceRow> rows);

// The cell of a row, or an empty text for a short row.
[[nodiscard]] const std::string& cellAt(const SourceRow& row, std::size_t column) noexcept;

} // namespace fdss::core
