#pragma once

#include "core/parsing/SourceTable.hpp"

#include <optional>
#include <string_view>
#include <vector>

// CSV of an import (SPEC section 8 step 2, RFC 4180 plus what spreadsheets write): fields in double
// quotes may hold the delimiter, line breaks and "" for a quote; a quote inside an unquoted field
// is an ordinary character; CRLF, LF and a lone CR end a record, and inside a quoted field each of
// them becomes '\n'. A quoted field left open at the end of the file is reported with the line it
// opens on; the reader refuses such a file.

namespace fdss::core {

inline constexpr char kDefaultDelimiter = ';';

// ';', ',' or a tab: the candidate found in the header line (outside quotes) whose count repeats in
// most of the first lines. None in the header: kDefaultDelimiter (a file with one column).
[[nodiscard]] char detectDelimiter(std::string_view utf8);

struct CsvRecords {
    // Every record with the line it starts on (1-based). Empty lines are kept as records without
    // cells, so the line numbers stay right; toSourceTable() drops blank rows.
    std::vector<SourceRow> rows;
    // The line of a quoted field still open at the end of the file; the rows are then incomplete.
    std::optional<int> unterminatedQuoteLine;
};

[[nodiscard]] CsvRecords parseCsv(std::string_view utf8, char delimiter);

} // namespace fdss::core
