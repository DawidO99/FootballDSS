#include "data/readers/XlsxReader.hpp"

#include "xlsxabstractsheet.h"
#include "xlsxcell.h"
#include "xlsxcelllocation.h"
#include "xlsxdocument.h"
#include "xlsxworkbook.h"
#include "xlsxworksheet.h"

#include <QBuffer>
#include <QDate>
#include <QTime>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fdss::data {

namespace {

// Doubles hold whole numbers exactly up to 2^53.
constexpr double kLargestExactInteger = 9007199254740992.0;

// A date cell holds a serial day number. Cell::dateTime() goes through local time and moves a
// date to the day before in summer time, so the date is computed here, without any time zone.
QString dateText(double serial, bool date1904) {
    if (serial < 1.0) {
        const auto minutes = static_cast<int>(std::lround(serial * 24 * 60)) % (24 * 60);
        return QTime(minutes / 60, minutes % 60).toString(QStringLiteral("HH:mm"));
    }
    const auto days = static_cast<qint64>(std::floor(serial + 1e-9));
    if (date1904) {
        return QDate(1904, 1, 1).addDays(days).toString(Qt::ISODate);
    }
    // Serial 60 is the 29 February 1900 that never was (kept by Excel for Lotus 1-2-3).
    const QDate epoch = days > 60 ? QDate(1899, 12, 30) : QDate(1899, 12, 31);
    return epoch.addDays(days).toString(Qt::ISODate);
}

QString cellText(const QXlsx::Cell& cell, bool date1904) {
    const QVariant value = cell.value();
    if (cell.isDateTime() && value.canConvert<double>()) {
        bool numeric = false;
        const double serial = value.toDouble(&numeric);
        if (numeric) {
            return dateText(serial, date1904);
        }
    }
    switch (value.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float: {
        const double number = value.toDouble();
        if (std::isfinite(number) && std::trunc(number) == number &&
            std::abs(number) < kLargestExactInteger) {
            return QString::number(static_cast<qlonglong>(number));
        }
        return QString::number(number, 'g', 15);
    }
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    default:
        return value.toString();
    }
}

// Only the cells the sheet stores, in row and column order: the reported dimension may cover whole
// columns (formatting), which a rows x columns walk would take minutes to cross. Rows after a gap
// of any length are kept; blank and formatted-only cells add nothing.
std::vector<core::SourceRow> readRows(const QXlsx::Document& document) {
    std::vector<core::SourceRow> rows;
    const QXlsx::Worksheet* sheet = document.currentWorksheet();
    if (sheet == nullptr) {
        return rows;
    }
    const bool date1904 = document.workbook() != nullptr && document.workbook()->isDate1904();
    int maxRow = 0;
    int maxColumn = 0;
    const QVector<QXlsx::CellLocation> cells = sheet->getFullCells(&maxRow, &maxColumn);

    struct TextCell {
        int row;
        int column;
        QString text;
    };
    std::vector<TextCell> texts;
    int firstColumn = std::numeric_limits<int>::max();
    for (const QXlsx::CellLocation& location : cells) {
        if (!location.cell) {
            continue;
        }
        QString text = cellText(*location.cell, date1904);
        if (text.trimmed().isEmpty()) {
            continue;
        }
        firstColumn = std::min(firstColumn, location.col);
        texts.push_back(TextCell{location.row, location.col, std::move(text)});
    }
    // getFullCells() returns the rows in order and the columns of a row in order.
    for (TextCell& cell : texts) {
        if (rows.empty() || rows.back().number != cell.row) {
            rows.push_back(core::SourceRow{cell.row, {}});
        }
        std::vector<std::string>& rowCells = rows.back().cells;
        rowCells.resize(static_cast<std::size_t>(cell.column - firstColumn), std::string{});
        rowCells.push_back(cell.text.toStdString());
    }
    return rows;
}

core::Error corrupt(const char* detail) {
    return core::Error{core::ErrorCode::UnsupportedFormat, detail, "import_xlsx_corrupt"};
}

} // namespace

core::Result<XlsxBook> readXlsx(const QByteArray& bytes, int sheet) {
    QByteArray copy = bytes; // QBuffer needs a non-const array; the copy is shared, not deep
    QBuffer buffer(&copy);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return std::unexpected(corrupt("cannot open the buffer"));
    }
    QXlsx::Document document(&buffer);
    if (!document.load()) {
        return std::unexpected(corrupt("not a readable xlsx workbook"));
    }
    XlsxBook book;
    for (const QString& name : document.sheetNames()) {
        const QXlsx::AbstractSheet* candidate = document.sheet(name);
        if (candidate != nullptr && candidate->sheetType() == QXlsx::AbstractSheet::ST_WorkSheet) {
            book.sheets.push_back(name);
        }
    }
    if (book.sheets.isEmpty()) {
        return std::unexpected(corrupt("the workbook has no worksheet"));
    }
    if (sheet < 0 || sheet >= book.sheets.size()) {
        return std::unexpected(
            core::Error{core::ErrorCode::InvalidInput, "no such sheet", "import_sheet"});
    }
    if (!document.selectSheet(book.sheets.at(sheet))) {
        return std::unexpected(corrupt("cannot select the sheet"));
    }
    book.sheet = sheet;
    book.rows = readRows(document);
    return book;
}

} // namespace fdss::data
