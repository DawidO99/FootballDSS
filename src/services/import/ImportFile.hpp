#pragma once

#include "core/parsing/SourceTable.hpp"
#include "core/parsing/TextEncoding.hpp"
#include "core/util/Error.hpp"

#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <string_view>

// Steps 1 and 2 of an import (SPEC section 8, AGENTS section 8): the type comes from the content
// (QMimeDatabase and the file signatures), never from the extension; at most 20 MB; CSV/TXT and
// XLSX are read, XLS and JSON are refused with their own messages. No database: safe on a worker
// thread.

namespace fdss::services {

inline constexpr qint64 kMaxImportFileBytes = 20LL * 1024 * 1024;
// The only file name extensions the import takes (P16 manual test), besides the content check.
inline constexpr std::array<std::string_view, 4> kImportExtensions = {"csv", "txt", "tsv", "xlsx"};

enum class ImportFileKind { Csv, Xlsx };

struct ImportSource {
    QString path;
    QString fileName; // without the folder, for import_log and the screen
    QString hash;     // SHA-256 of the bytes, lower-case hex
    ImportFileKind kind = ImportFileKind::Csv;
    core::TextEncoding encoding = core::TextEncoding::Utf8; // CSV only
    char delimiter = ';';                                   // CSV only
    QStringList sheets;                                     // XLSX only
    int sheet = 0;                                          // XLSX only
    // The table of the sheet, or why it has none (an XLSX sheet that is empty or holds only a
    // header): the file itself is fine and another sheet may be chosen.
    core::Status tableStatus;
    core::SourceTable table;
    // The line of the file tableStatus refers to: the opening line of an unterminated CSV quote
    // ("import_csv_quote"). Error::field stays a field name; the wizard puts the line into its
    // message.
    std::optional<int> errorLine;
};

// File errors (field):
// - cannot be read: Io ("import_file");
// - larger than kMaxImportFileBytes: InvalidInput ("import_file_size");
// - empty or only blank rows: InvalidInput ("import_empty"); a header only: ("import_no_rows");
// - XLS: UnsupportedFormat ("import_xls"); JSON: UnsupportedFormat ("import_json");
// - another extension than kImportExtensions, or content that is not text or XLSX:
//   UnsupportedFormat ("import_file_type"); XLS and JSON content are told apart first;
// - a broken XLSX: UnsupportedFormat ("import_xlsx_corrupt").
// A CSV with a quoted field left open is read, but has no table: tableStatus is InvalidInput
// ("import_csv_quote") and errorLine the line the field opens on.
// `sheet` (XLSX): nullopt takes the first sheet with data rows, never an instruction sheet
// ("Instrukcja" of the template); a chosen sheet without them keeps
// the error in tableStatus. Outside the sheets: InvalidInput ("import_sheet").
[[nodiscard]] core::Result<ImportSource> readImportFile(const QString& path,
                                                        std::optional<int> sheet = std::nullopt);

} // namespace fdss::services
