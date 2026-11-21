#include "core/parsing/SourceTable.hpp"

#include "core/domain/Validation.hpp"

#include <algorithm>
#include <utility>

namespace fdss::core {

namespace {

bool isBlank(const SourceRow& row) {
    return std::ranges::all_of(row.cells,
                               [](const std::string& cell) { return trimmed(cell).empty(); });
}

// Columns up to the last one with text in any row.
std::size_t usedWidth(const std::vector<SourceRow>& rows) {
    std::size_t width = 0;
    for (const SourceRow& row : rows) {
        for (std::size_t column = row.cells.size(); column > width; --column) {
            if (!trimmed(row.cells[column - 1]).empty()) {
                width = column;
                break;
            }
        }
    }
    return width;
}

} // namespace

Result<SourceTable> toSourceTable(std::vector<SourceRow> rows) {
    std::erase_if(rows, isBlank);
    if (rows.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidInput, "no row with text", "import_empty"});
    }
    if (rows.size() == 1) {
        return std::unexpected(
            Error{ErrorCode::InvalidInput, "a header without rows", "import_no_rows"});
    }
    const std::size_t width = usedWidth(rows);
    SourceTable table;
    table.headerRow = rows.front().number;
    table.headers.reserve(width);
    for (std::size_t column = 0; column < width; ++column) {
        table.headers.push_back(trimmed(cellAt(rows.front(), column)));
    }
    table.rows.reserve(rows.size() - 1);
    for (auto row = std::next(rows.begin()); row != rows.end(); ++row) {
        if (row->cells.size() > width) {
            row->cells.resize(width);
        }
        table.rows.push_back(std::move(*row));
    }
    return table;
}

const std::string& cellAt(const SourceRow& row, std::size_t column) noexcept {
    static const std::string kEmpty;
    return column < row.cells.size() ? row.cells[column] : kEmpty;
}

} // namespace fdss::core
