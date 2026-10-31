// P03 spike: proves that QXlsx builds against Qt 6.12 and reads a real spreadsheet file.
// tests/fixtures/sample.xlsx was saved by LibreOffice Calc (not by QXlsx), header + 2 players.

#include <xlsxcell.h>
#include <xlsxdocument.h>

#include <QDir>
#include <QString>
#include <QTemporaryFile>

#include <gtest/gtest.h>

namespace {

const QString kSamplePath = QStringLiteral(FDSS_FIXTURES_DIR "/sample.xlsx");

} // namespace

TEST(XlsxSpikeTest, OpensFixture) {
    const QXlsx::Document document(kSamplePath);
    ASSERT_TRUE(document.load());
    EXPECT_EQ(document.sheetNames(), QStringList{QStringLiteral("sample")});
    EXPECT_EQ(document.dimension().rowCount(), 3);
    EXPECT_EQ(document.dimension().columnCount(), 4);
}

TEST(XlsxSpikeTest, ReadsHeaderRow) {
    const QXlsx::Document document(kSamplePath);
    ASSERT_TRUE(document.load());
    EXPECT_EQ(document.read(1, 1).toString(), QStringLiteral("Imię"));
    EXPECT_EQ(document.read(1, 2).toString(), QStringLiteral("Nazwisko"));
    EXPECT_EQ(document.read(1, 3).toString(), QStringLiteral("Numer"));
    EXPECT_EQ(document.read(1, 4).toString(), QStringLiteral("Rok urodzenia"));
}

TEST(XlsxSpikeTest, ReadsPolishNames) {
    const QXlsx::Document document(kSamplePath);
    ASSERT_TRUE(document.load());
    EXPECT_EQ(document.read(2, 1).toString(), QStringLiteral("Łukasz"));
    EXPECT_EQ(document.read(2, 2).toString(), QStringLiteral("Żółkiewski"));
    EXPECT_EQ(document.read(3, 1).toString(), QStringLiteral("Józef"));
    EXPECT_EQ(document.read(3, 2).toString(), QStringLiteral("Ćwiękała"));
}

TEST(XlsxSpikeTest, ReadsNumericCells) {
    const QXlsx::Document document(kSamplePath);
    ASSERT_TRUE(document.load());
    const auto shirtNumber = document.cellAt(2, 3);
    ASSERT_NE(shirtNumber, nullptr);
    EXPECT_EQ(shirtNumber->cellType(), QXlsx::Cell::NumberType);
    EXPECT_EQ(shirtNumber->value().toInt(), 9);
    EXPECT_EQ(document.read(2, 4).toInt(), 2001);
    EXPECT_EQ(document.read(3, 3).toInt(), 1);
    EXPECT_EQ(document.read(3, 4).toInt(), 1998);
}

TEST(XlsxSpikeTest, MissingFileFailsCleanly) {
    const QXlsx::Document document(QStringLiteral(FDSS_FIXTURES_DIR "/does-not-exist.xlsx"));
    EXPECT_FALSE(document.load());
}

TEST(XlsxSpikeTest, NonZipFileFailsCleanly) {
    QTemporaryFile file(QDir::tempPath() + QStringLiteral("/fdss-XXXXXX.xlsx"));
    ASSERT_TRUE(file.open());
    file.write("Imię;Nazwisko\nŁukasz;Żółkiewski\n");
    file.close();
    const QXlsx::Document document(file.fileName());
    EXPECT_FALSE(document.load());
}
