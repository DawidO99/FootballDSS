#include "data/readers/XlsxReader.hpp"

#include "TestDatabase.hpp"

#include <gtest/gtest.h>

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

} // namespace
