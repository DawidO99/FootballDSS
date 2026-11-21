#include "data/readers/XlsxReader.hpp"

#include "TestDatabase.hpp"
#include "xlsxcellrange.h"
#include "xlsxdocument.h"
#include "xlsxformat.h"
#include "xlsxworksheet.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QElapsedTimer>
#include <QtConcurrentRun>

#include <string>
#include <vector>

using namespace fdss;
using namespace fdss::data;

namespace {

QByteArray fixture(const char* name) {
    return tests::readFile(
        QStringLiteral(FDSS_IMPORT_FIXTURES_DIR "/%1").arg(QLatin1StringView(name)));
}

using Cells = std::vector<std::string>;

// Made by LibreOffice Calc: sheet "Notatki" with one text cell, then "Kadra" with a blank first
// row, a header and three players; numbers are numeric cells, one birth date a date cell.
TEST(XlsxReaderTest, ListsSheetsAndReadsTheChosenOne) {
    const auto book = readXlsx(fixture("multi-sheet.xlsx"), 1);
    ASSERT_TRUE(book.has_value()) << book.error().detail;
    EXPECT_EQ(book->sheets, (QStringList{QStringLiteral("Notatki"), QStringLiteral("Kadra")}));
    EXPECT_EQ(book->sheet, 1);
    ASSERT_EQ(book->rows.size(), 4U);
    EXPECT_EQ(book->rows[0].number, 2);
    EXPECT_EQ(book->rows[0].cells, (Cells{"Imię", "Nazwisko", "Nr", "Data urodzenia", "Pozycja"}));
    EXPECT_EQ(book->rows[1].cells, (Cells{"Łukasz", "Żółkiewski", "9", "2001-05-12", "N"}));
    EXPECT_EQ(book->rows[2].cells, (Cells{"Józef", "Ćwiękała", "1", "1998", "BR"}));
    EXPECT_EQ(book->rows[3].cells, (Cells{"Paweł", "Śliwiński", "4", "", "ŚO"}));
}

TEST(XlsxReaderTest, FirstSheet) {
    const auto book = readXlsx(fixture("multi-sheet.xlsx"), 0);
    ASSERT_TRUE(book.has_value()) << book.error().detail;
    ASSERT_EQ(book->rows.size(), 1U);
    EXPECT_EQ(book->rows[0].cells, (Cells{"Kadra na sezon 2026/27"}));
}

TEST(XlsxReaderTest, SheetOutsideTheBook) {
    const auto book = readXlsx(fixture("multi-sheet.xlsx"), 2);
    ASSERT_FALSE(book.has_value());
    EXPECT_EQ(book.error().code, core::ErrorCode::InvalidInput);
    EXPECT_EQ(book.error().field, "import_sheet");
}

TEST(XlsxReaderTest, TruncatedFileIsCorrupt) {
    const auto book = readXlsx(fixture("corrupted.xlsx"), 0);
    ASSERT_FALSE(book.has_value());
    EXPECT_EQ(book.error().code, core::ErrorCode::UnsupportedFormat);
    EXPECT_EQ(book.error().field, "import_xlsx_corrupt");
}

// The import reads on the thread pool (AGENTS section 5).
TEST(XlsxReaderTest, ReadsOnAWorkerThread) {
    const QByteArray bytes = fixture("multi-sheet.xlsx");
    auto future = QtConcurrent::run([bytes] { return readXlsx(bytes, 1); });
    const auto book = future.result();
    ASSERT_TRUE(book.has_value()) << book.error().detail;
    EXPECT_EQ(book->rows.size(), 4U);
}

TEST(XlsxReaderTest, ReadsDataAfterMoreThanAThousandBlankRows) {
    const auto book = readXlsx(fixture("blank-gap.xlsx"), 0);
    ASSERT_TRUE(book.has_value()) << book.error().detail;
    ASSERT_EQ(book->rows.size(), 3U);
    EXPECT_EQ(book->rows.back().number, 1003);
    EXPECT_EQ(book->rows.back().cells, (Cells{"Adam", "Nowak", "N"}));
}

// A workbook written by QXlsx: `fill` writes the cells of the first sheet.
template <class Fill> QByteArray workbook(Fill fill) {
    QXlsx::Document document;
    fill(*document.currentWorksheet());
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    EXPECT_TRUE(document.saveAs(&buffer));
    return buffer.data();
}

void writePlayer(QXlsx::Worksheet& sheet, int row, const char* first, const char* last) {
    sheet.write(row, 1, QString::fromUtf8(first));
    sheet.write(row, 2, QString::fromUtf8(last));
    sheet.write(row, 3, QStringLiteral("N"));
}

// P16 review follow-up 4: only the stored cells are read, so a gap of any length keeps the rows
// after it, and a sheet whose dimension covers whole columns (formatting) stays fast.
TEST(XlsxReaderTest, KeepsRowsAfterFifteenHundredEmptyRows) {
    const QByteArray bytes = workbook([](QXlsx::Worksheet& sheet) {
        writePlayer(sheet, 1, "Imię", "Nazwisko");
        writePlayer(sheet, 2, "Jan", "Kowalski");
        writePlayer(sheet, 1503, "Adam", "Nowak");
    });
    const auto book = readXlsx(bytes, 0);
    ASSERT_TRUE(book.has_value()) << book.error().detail;
    ASSERT_EQ(book->rows.size(), 3U);
    EXPECT_EQ(book->rows.back().number, 1503);
    EXPECT_EQ(book->rows.back().cells, (Cells{"Adam", "Nowak", "N"}));
}

TEST(XlsxReaderTest, WholeColumnDimensionIsReadQuickly) {
    const QByteArray bytes = workbook([](QXlsx::Worksheet& sheet) {
        writePlayer(sheet, 1, "Imię", "Nazwisko");
        writePlayer(sheet, 2, "Jan", "Kowalski");
        writePlayer(sheet, 900000, "Adam", "Nowak");
        QXlsx::Format formatted;
        formatted.setFontBold(true);
        // Formatted but empty: the dimension now spans A1:Z1048576.
        sheet.writeBlank(1048576, 26, formatted);
    });
    {
        QByteArray copy = bytes;
        QBuffer buffer(&copy);
        buffer.open(QIODevice::ReadOnly);
        const QXlsx::Document document(&buffer);
        ASSERT_EQ(document.dimension().lastRow(), 1048576);
        ASSERT_EQ(document.dimension().lastColumn(), 26);
    }
    QElapsedTimer timer;
    timer.start();
    const auto book = readXlsx(bytes, 0);
    // A walk over rows x columns of the dimension (27 million lookups) takes far longer.
    EXPECT_LT(timer.elapsed(), 2000);
    ASSERT_TRUE(book.has_value()) << book.error().detail;
    ASSERT_EQ(book->rows.size(), 3U);
    EXPECT_EQ(book->rows.back().number, 900000);
    EXPECT_EQ(book->rows.back().cells, (Cells{"Adam", "Nowak", "N"}));
}

} // namespace
