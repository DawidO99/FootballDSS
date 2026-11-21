#include "core/parsing/Csv.hpp"

#include <array>
#include <cstddef>

namespace fdss::core {

namespace {

constexpr std::array<char, 3> kDelimiters = {';', ',', '\t'};
// Lines looked at by detectDelimiter(), the header included.
constexpr std::size_t kSampleLines = 20;

using DelimiterCounts = std::array<int, kDelimiters.size()>;

struct SampleLine {
    DelimiterCounts counts{};
    bool empty = true;
};

// Counts the candidates outside quotes in the first lines, a quoted line break staying inside
// its line.
std::vector<SampleLine> sampleLines(std::string_view text) {
    std::vector<SampleLine> lines(1);
    bool inQuotes = false;
    for (std::size_t index = 0; index < text.size() && lines.size() <= kSampleLines; ++index) {
        const char character = text[index];
        if (character == '"') {
            inQuotes = !inQuotes;
            lines.back().empty = false;
            continue;
        }
        if (inQuotes) {
            continue;
        }
        if (character == '\r' || character == '\n') {
            if (character == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            lines.emplace_back();
            continue;
        }
        lines.back().empty = false;
        for (std::size_t candidate = 0; candidate < kDelimiters.size(); ++candidate) {
            if (character == kDelimiters[candidate]) {
                ++lines.back().counts[candidate];
            }
        }
    }
    std::erase_if(lines, [](const SampleLine& line) { return line.empty; });
    if (lines.size() > kSampleLines) {
        lines.resize(kSampleLines);
    }
    return lines;
}

} // namespace

char detectDelimiter(std::string_view utf8) {
    const std::vector<SampleLine> lines = sampleLines(utf8);
    if (lines.empty()) {
        return kDefaultDelimiter;
    }
    const DelimiterCounts& header = lines.front().counts;
    std::size_t best = kDelimiters.size();
    int bestConsistent = -1;
    for (std::size_t candidate = 0; candidate < kDelimiters.size(); ++candidate) {
        if (header[candidate] == 0) {
            continue;
        }
        int consistent = 0;
        for (std::size_t line = 1; line < lines.size(); ++line) {
            if (lines[line].counts[candidate] == header[candidate]) {
                ++consistent;
            }
        }
        // Ties: more columns in the header, then the order of kDelimiters.
        if (consistent > bestConsistent ||
            (consistent == bestConsistent && header[candidate] > header[best])) {
            best = candidate;
            bestConsistent = consistent;
        }
    }
    return best == kDelimiters.size() ? kDefaultDelimiter : kDelimiters[best];
}

CsvRecords parseCsv(std::string_view utf8, char delimiter) {
    std::vector<SourceRow> rows;
    SourceRow row{1, {}};
    std::string field;
    bool fieldStarted = false; // the record has at least one field, even an empty one
    bool inQuotes = false;
    int line = 1;
    int quoteLine = 0;

    const auto endField = [&] {
        row.cells.push_back(std::move(field));
        field.clear();
        fieldStarted = true;
    };
    const auto endRecord = [&] {
        if (fieldStarted || !field.empty()) {
            endField();
        }
        rows.push_back(std::move(row));
        row = SourceRow{line, {}};
        fieldStarted = false;
    };

    std::size_t index = 0;
    while (index < utf8.size()) {
        const char character = utf8[index];
        if (inQuotes) {
            if (character == '"') {
                if (index + 1 < utf8.size() && utf8[index + 1] == '"') {
                    field.push_back('"');
                    index += 2;
                    continue;
                }
                inQuotes = false;
                ++index;
                continue;
            }
            if (character == '\n' ||
                (character == '\r' && (index + 1 >= utf8.size() || utf8[index + 1] != '\n'))) {
                ++line;
            }
            field.push_back(character);
            ++index;
            continue;
        }
        if (character == '"' && field.empty()) {
            inQuotes = true;
            quoteLine = line;
            fieldStarted = true;
            ++index;
            continue;
        }
        if (character == delimiter) {
            endField();
            ++index;
            continue;
        }
        if (character == '\r' || character == '\n') {
            if (character == '\r' && index + 1 < utf8.size() && utf8[index + 1] == '\n') {
                ++index;
            }
            ++index;
            ++line;
            endRecord();
            continue;
        }
        field.push_back(character);
        ++index;
    }
    if (inQuotes) {
        return CsvRecords{std::move(rows), quoteLine};
    }
    // The last record has no line break after it.
    if (fieldStarted || !field.empty()) {
        endRecord();
    }
    return CsvRecords{std::move(rows), std::nullopt};
}

} // namespace fdss::core
