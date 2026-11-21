#pragma once

#include "core/parsing/SourceTable.hpp"
#include "core/util/Error.hpp"

#include <QByteArray>
#include <QStringList>

#include <vector>

// XLSX of an import (SPEC section 8 step 2; ADR-006: QXlsx stays in the data layer). Every cell
// becomes text: whole numbers without decimals ("7", not "7.0"), dates as ISO yyyy-MM-dd, times
// HH:mm. Safe on a worker thread: nothing here touches the database.

namespace fdss::data {

struct XlsxBook {
    QStringList sheets; // worksheets only, in workbook order
    int sheet = 0;      // index into `sheets` of the rows below
    std::vector<core::SourceRow> rows;
};

// `bytes` must start with the ZIP signature (the caller checked it), otherwise QXlsx prints a
// warning. Not a readable workbook, or one without a worksheet: UnsupportedFormat with field
// "import_xlsx_corrupt". `sheet` outside the sheets: InvalidInput with field "import_sheet".
[[nodiscard]] core::Result<XlsxBook> readXlsx(const QByteArray& bytes, int sheet);

} // namespace fdss::data
