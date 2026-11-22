#include "core/parsing/Csv.hpp"
#include "core/parsing/HeaderMatching.hpp"
#include "core/parsing/PositionSynonyms.hpp"
#include "core/parsing/SourceTable.hpp"
#include "core/parsing/TextEncoding.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

using namespace fdss::core;

namespace {

using Cells = std::vector<std::string>;

std::vector<SourceRow> parsedRows(std::string_view text, char delimiter) {
    CsvRecords result = parseCsv(text, delimiter);
    EXPECT_FALSE(result.unterminatedQuoteLine.has_value());
    return std::move(result.rows);
}

std::vector<Cells> cellsOf(const std::vector<SourceRow>& rows) {
    std::vector<Cells> result;
    for (const SourceRow& row : rows) {
        result.push_back(row.cells);
    }
    return result;
}

// --- Encoding ---

TEST(TextEncodingTest, PlainUtf8StaysAsItIs) {
    const DecodedText text = decodeText("Imię;Łukasz");
    EXPECT_EQ(text.encoding, TextEncoding::Utf8);
    EXPECT_EQ(text.utf8, "Imię;Łukasz");
}

TEST(TextEncodingTest, Utf8BomIsRemoved) {
    const DecodedText text = decodeText("\xEF\xBB\xBFNazwisko;Imię");
    EXPECT_EQ(text.encoding, TextEncoding::Utf8Bom);
    EXPECT_EQ(text.utf8, "Nazwisko;Imię");
}

TEST(TextEncodingTest, InvalidUtf8IsReadAsCp1250) {
    // "Żółć ŁĄŚ ąęś" as Polish Excel saves it.
    const DecodedText text = decodeText("\xAF\xF3\xB3\xE6 \xA3\xA5\x8C \xB9\xEA\x9C");
    EXPECT_EQ(text.encoding, TextEncoding::Cp1250);
    EXPECT_EQ(text.utf8, "Żółć ŁĄŚ ąęś");
}

TEST(TextEncodingTest, Cp1250CoversEveryPolishLetter) {
    EXPECT_EQ(decodeCp1250("\xA5\xC6\xCA\xA3\xD1\xD3\x8C\x8F\xAF"), "ĄĆĘŁŃÓŚŹŻ");
    EXPECT_EQ(decodeCp1250("\xB9\xE6\xEA\xB3\xF1\xF3\x9C\x9F\xBF"), "ąćęłńóśźż");
}

TEST(TextEncodingTest, Cp1250BytesWithoutCharacterBecomeReplacement) {
    EXPECT_EQ(decodeCp1250("a\x81z"), "a\xEF\xBF\xBDz");
}

TEST(TextEncodingTest, Utf16WithBomIsDecoded) {
    const std::string little("\xFF\xFE"
                             "A\0\x05\x01;\0",
                             8);
    EXPECT_EQ(decodeText(little).utf8, "Aą;");
    EXPECT_EQ(decodeText(little).encoding, TextEncoding::Utf16);
    const std::string big("\xFE\xFF\0A\x01\x05", 6);
    EXPECT_EQ(decodeText(big).utf8, "Aą");
}

TEST(TextEncodingTest, Utf8ValidationRejectsOverlongAndTruncatedForms) {
    EXPECT_TRUE(isValidUtf8("zażółć"));
    EXPECT_FALSE(isValidUtf8("\xC0\xAF"));
    EXPECT_FALSE(isValidUtf8("\xE2\x82"));
    EXPECT_FALSE(isValidUtf8("\xED\xA0\x80")); // a surrogate
}

// --- CSV ---

TEST(CsvTest, DetectsSemicolonOfPolishExcel) {
    EXPECT_EQ(detectDelimiter("Nazwisko;Imię;Nr\nKowalski;Jan;7\nNowak;Adam;9,5\n"), ';');
}

TEST(CsvTest, DetectsCommaAndTab) {
    EXPECT_EQ(detectDelimiter("first,last,number\nJan,Kowalski,7\n"), ',');
    EXPECT_EQ(detectDelimiter("first\tlast\nJan\tKowalski\n"), '\t');
}

TEST(CsvTest, IgnoresDelimitersInsideQuotes) {
    EXPECT_EQ(detectDelimiter("\"Kowalski, Jan\";Nr\n\"Nowak, Adam\";9\n"), ';');
}

TEST(CsvTest, OneColumnGetsTheDefault) {
    EXPECT_EQ(detectDelimiter("Zawodnik\nJan Kowalski\n"), kDefaultDelimiter);
    EXPECT_EQ(detectDelimiter(""), kDefaultDelimiter);
}

TEST(CsvTest, SplitsRecordsAndFields) {
    const auto rows = parsedRows("a;b;c\r\n1;2;3\n", ';');
    EXPECT_EQ(cellsOf(rows), (std::vector<Cells>{{"a", "b", "c"}, {"1", "2", "3"}}));
    EXPECT_EQ(rows[1].number, 2);
}

TEST(CsvTest, QuotesKeepDelimitersLineBreaksAndQuotes) {
    const auto rows =
        parsedRows("\"Kowalski; Jan\",\"say \"\"hi\"\"\",\"two\nlines\"\nnext,x,y", ',');
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].cells, (Cells{"Kowalski; Jan", "say \"hi\"", "two\nlines"}));
    // The record after a quoted line break starts on line 3.
    EXPECT_EQ(rows[1].number, 3);
    EXPECT_EQ(rows[1].cells, (Cells{"next", "x", "y"}));
}

TEST(CsvTest, LineBreaksInsideQuotedFieldBecomeLineFeeds) {
    // Excel on Windows writes CRLF inside a field; old Mac files a lone CR.
    const auto rows = parsedRows("a;\"one\r\ntwo\rthree\nfour\"\r\nnext;x\r\n", ';');
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].cells, (Cells{"a", "one\ntwo\nthree\nfour"}));
    // Each break counts as one line, CRLF included.
    EXPECT_EQ(rows[1].number, 5);
    EXPECT_EQ(rows[1].cells, (Cells{"next", "x"}));
}

TEST(CsvTest, CommaInsideQuotedFieldWithCommaDelimiter) {
    const auto rows = parsedRows("name,nr\n\"Nowak, Adam\",9\n", ',');
    EXPECT_EQ(rows[1].cells, (Cells{"Nowak, Adam", "9"}));
}

TEST(CsvTest, EmptyFieldsAndEmptyLines) {
    const auto rows = parsedRows("a;;c\n\n;;\n", ';');
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].cells, (Cells{"a", "", "c"}));
    EXPECT_TRUE(rows[1].cells.empty());
    EXPECT_EQ(rows[2].cells, (Cells{"", "", ""}));
    EXPECT_EQ(rows[2].number, 3);
}

TEST(CsvTest, QuoteInsideUnquotedFieldIsText) {
    EXPECT_EQ(parsedRows("12\" tall;x", ';')[0].cells, (Cells{"12\" tall", "x"}));
}

TEST(CsvTest, OpenQuoteFailsTheFileWithTheOpeningLine) {
    // The line travels on its own, not in an Error field (P16 review follow-up 5).
    EXPECT_EQ(parseCsv("a;b\r\n1;\"open\nb;c\n", ';').unterminatedQuoteLine, 2);
    EXPECT_EQ(parseCsv("\"open;b\n1;2\n", ';').unterminatedQuoteLine, 1);
    EXPECT_EQ(parseCsv("a;\"closed\"\n", ';').unterminatedQuoteLine, std::nullopt);
}

TEST(CsvTest, LoneCarriageReturnEndsARecord) {
    EXPECT_EQ(cellsOf(parsedRows("a\rb", ';')), (std::vector<Cells>{{"a"}, {"b"}}));
}

// --- Source table ---

TEST(SourceTableTest, FirstNonBlankRowIsTheHeader) {
    const auto table = toSourceTable(parsedRows(";;\n Nazwisko ;Imię;\n\nKowalski;Jan;\n", ';'));
    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->headerRow, 2);
    EXPECT_EQ(table->headers, (Cells{"Nazwisko", "Imię"}));
    ASSERT_EQ(table->rows.size(), 1U);
    EXPECT_EQ(table->rows[0].number, 4);
    EXPECT_EQ(table->rows[0].cells, (Cells{"Kowalski", "Jan"}));
}

TEST(SourceTableTest, EmptyFileAndHeaderOnly) {
    const auto empty = toSourceTable(parsedRows(" \n;;\n", ';'));
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().field, "import_empty");
    const auto headerOnly = toSourceTable(parsedRows("Nazwisko;Imię\n", ';'));
    ASSERT_FALSE(headerOnly.has_value());
    EXPECT_EQ(headerOnly.error().field, "import_no_rows");
}

TEST(SourceTableTest, DataBeyondTheHeaderKeepsAnEmptyHeader) {
    const auto table = toSourceTable(parsedRows("a;b\n1;2;3\n", ';'));
    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->headers, (Cells{"a", "b", ""}));
    EXPECT_EQ(cellAt(table->rows[0], 2), "3");
    EXPECT_EQ(cellAt(table->rows[0], 7), "");
}

// --- Headers ---

TEST(HeaderMatchingTest, FoldsCaseDiacriticsAndPunctuation) {
    EXPECT_EQ(foldText("  IMIĘ: "), "imie");
    EXPECT_EQ(foldText("Nazwisko i imię"), "nazwisko i imie");
    EXPECT_EQ(foldText("Rok_urodzenia"), "rok urodzenia");
    EXPECT_EQ(foldText("Nr."), "nr");
    EXPECT_EQ(foldText("#"), "#");
    EXPECT_EQ(foldText("ŻÓŁTA Łódź ß"), "zolta lodz s");
}

TEST(HeaderMatchingTest, NamesPreserveLettersFromEveryScript) {
    EXPECT_EQ(foldName(" ŻÓŁKIEWSKI "), "zolkiewski");
    for (const std::string name :
         {"Андрій", "Шевченко", "Іван", "Петренко", "王", "محمد", "Νίκος"}) {
        EXPECT_EQ(foldName(name), name);
    }
    EXPECT_TRUE(foldName(" -- ").empty());
}

TEST(HeaderMatchingTest, FirstHeaderWinsAField) {
    constexpr std::array<std::string_view, 2> kNames = {"nr", "numer"};
    const std::array fields = {FieldSynonyms{7, kNames}};
    const std::vector<std::string> headers = {"Numer", "Nr", "Inne"};
    const auto matched = matchHeaders(headers, fields);
    EXPECT_EQ(matched, (std::vector<std::optional<int>>{7, std::nullopt, std::nullopt}));
}

TEST(HeaderMatchingTest, HeaderKeyIgnoresSpellingVariants) {
    const std::vector<std::string> a = {"Imię", "NAZWISKO", "Nr."};
    const std::vector<std::string> b = {"imie ", "Nazwisko", "nr"};
    EXPECT_EQ(headerKey(a), headerKey(b));
    EXPECT_EQ(headerKey(a), "imie|nazwisko|nr");
}

// --- Positions and foot ---

TEST(PositionSynonymsTest, PolishAndEnglishCodes) {
    EXPECT_EQ(parsePosition("BR"), Position::Gk);
    EXPECT_EQ(parsePosition("gk"), Position::Gk);
    EXPECT_EQ(parsePosition("ŚO"), Position::Cb);
    EXPECT_EQ(parsePosition("so"), Position::Cb);
    EXPECT_EQ(parsePosition("CB"), Position::Cb);
    EXPECT_EQ(parsePosition("N"), Position::St);
    EXPECT_EQ(parsePosition("ST"), Position::St);
    EXPECT_EQ(parsePosition("ŚPO"), Position::Am);
    EXPECT_EQ(parsePosition("ŚPD"), Position::Dm);
    EXPECT_EQ(parsePosition("LO"), Position::Lb);
    EXPECT_EQ(parsePosition("PO"), Position::Rb);
    EXPECT_EQ(parsePosition("LW"), Position::Lw);
}

TEST(PositionSynonymsTest, Names) {
    EXPECT_EQ(parsePosition("Bramkarz"), Position::Gk);
    EXPECT_EQ(parsePosition("środkowy obrońca"), Position::Cb);
    EXPECT_EQ(parsePosition("Napastnik"), Position::St);
    EXPECT_EQ(parsePosition("Centre Back"), Position::Cb);
    EXPECT_EQ(parsePosition("left winger"), Position::Lw);
}

TEST(PositionSynonymsTest, UnknownAndEmpty) {
    EXPECT_EQ(parsePosition("XYZ"), std::nullopt);
    EXPECT_EQ(parsePosition(""), std::nullopt);
    EXPECT_EQ(parsePosition("skrzydłowy"), std::nullopt); // which side?
}

TEST(PositionSynonymsTest, ListsKeepOrderAndCollectUnknown) {
    const PositionList list = parsePositionList("ŚO / PO, cb; xx |N");
    EXPECT_EQ(list.positions, (std::vector<Position>{Position::Cb, Position::Rb, Position::St}));
    EXPECT_EQ(list.unknown, (std::vector<std::string>{"xx"}));
    EXPECT_TRUE(parsePositionList("").positions.empty());
}

TEST(PositionSynonymsTest, Foot) {
    EXPECT_EQ(parseFoot("L"), PreferredFoot::Left);
    EXPECT_EQ(parseFoot("prawa"), PreferredFoot::Right);
    EXPECT_EQ(parseFoot("R"), PreferredFoot::Right);
    EXPECT_EQ(parseFoot("obunożny"), PreferredFoot::Both);
    EXPECT_EQ(parseFoot("L/P"), PreferredFoot::Both);
    EXPECT_EQ(parseFoot("both"), PreferredFoot::Both);
    EXPECT_EQ(parseFoot("lewa?"), PreferredFoot::Left);
    EXPECT_EQ(parseFoot("hand"), std::nullopt);
}

} // namespace
