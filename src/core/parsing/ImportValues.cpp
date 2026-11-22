#include "core/parsing/ImportValues.hpp"

#include "core/domain/Validation.hpp"
#include "core/parsing/HeaderMatching.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <vector>

namespace fdss::core {

namespace {

bool allDigits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

std::optional<int> toInt(std::string_view digits) {
    if (!allDigits(digits) || digits.size() > 9) {
        return std::nullopt;
    }
    int value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<Date> makeDate(int year, int month, int day) {
    const Date date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                    std::chrono::day{static_cast<unsigned>(day)}};
    if (!isSupportedDate(date)) {
        return std::nullopt;
    }
    return date;
}

// "45678" or "45678.75" (a date with a time). Serial 60 is the 29 February 1900 that never was.
std::optional<Date> fromExcelSerial(std::string_view text) {
    std::string_view whole = text;
    if (const std::size_t point = text.find_first_of(".,"); point != std::string_view::npos) {
        if (!allDigits(text.substr(point + 1))) {
            return std::nullopt;
        }
        whole = text.substr(0, point);
    }
    const auto serial = toInt(whole);
    if (!serial || *serial < 1 || *serial > kMaxExcelSerial) {
        return std::nullopt;
    }
    using namespace std::chrono;
    const sys_days epoch =
        *serial > 60 ? sys_days{year{1899} / December / 30} : sys_days{year{1899} / December / 31};
    const Date date{epoch + days{*serial}};
    if (!isSupportedDate(date)) {
        return std::nullopt;
    }
    return date;
}

std::vector<std::string_view> splitAt(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    while (true) {
        const std::size_t at = text.find(separator);
        parts.push_back(text.substr(0, at));
        if (at == std::string_view::npos) {
            return parts;
        }
        text = text.substr(at + 1);
    }
}

constexpr std::array<std::string_view, 9> kYes = {"tak", "t",    "yes",    "y",    "1",
                                                  "x",   "true", "prawda", "start"};
constexpr std::array<std::string_view, 7> kNo = {"nie", "n", "no", "0", "false", "falsz", "-"};

} // namespace

std::optional<Date> parseImportDate(std::string_view text, bool excelSerial) {
    std::string value = trimmed(text);
    if (excelSerial) {
        if (auto serial = fromExcelSerial(value)) {
            return serial;
        }
    }
    if (const std::size_t time = value.find_first_of("T "); time != std::string::npos) {
        value.resize(time);
    }
    for (const char separator : {'-', '.', '/'}) {
        const std::vector<std::string_view> parts = splitAt(value, separator);
        if (parts.size() != 3 || !std::ranges::all_of(parts, allDigits)) {
            continue;
        }
        if (separator == '-') {
            // yyyy-MM-dd only: 02-03-2026 is not a format of SPEC section 8.
            if (parts[0].size() != 4 || parts[1].size() > 2 || parts[2].size() > 2) {
                return std::nullopt;
            }
            return makeDate(*toInt(parts[0]), *toInt(parts[1]), *toInt(parts[2]));
        }
        if (parts[0].size() > 2 || parts[1].size() > 2 || parts[2].size() != 4) {
            return std::nullopt;
        }
        return makeDate(*toInt(parts[2]), *toInt(parts[1]), *toInt(parts[0]));
    }
    return std::nullopt;
}

std::optional<TimeOfDay> parseImportTime(std::string_view text) {
    const std::string value = trimmed(text);
    const char separator = value.find(':') != std::string::npos ? ':' : '.';
    const std::vector<std::string_view> parts = splitAt(value, separator);
    if (parts.size() < 2 || parts.size() > 3 || parts[0].size() > 2 || parts[1].size() != 2 ||
        (parts.size() == 3 && parts[2].size() != 2)) {
        return std::nullopt;
    }
    const auto hours = toInt(parts[0]);
    const auto minutes = toInt(parts[1]);
    const auto seconds = parts.size() == 3 ? toInt(parts[2]) : std::optional<int>(0);
    if (!hours || !minutes || !seconds || *hours > 23 || *minutes > 59 || *seconds > 59) {
        return std::nullopt;
    }
    return TimeOfDay{*hours * 60 + *minutes};
}

std::optional<bool> parseYesNo(std::string_view text) {
    const std::string folded = foldText(text);
    // foldText() drops '-' and leaves an empty text; an empty cell is no answer.
    if (folded.empty()) {
        return trimmed(text) == "-" ? std::optional<bool>(false) : std::nullopt;
    }
    if (std::ranges::find(kYes, folded) != kYes.end()) {
        return true;
    }
    if (std::ranges::find(kNo, folded) != kNo.end()) {
        return false;
    }
    return std::nullopt;
}

bool looksLikeWideFormat(const SourceTable& table) {
    const auto dates = std::ranges::count_if(table.headers, [](const std::string& header) {
        return parseImportDate(header, false).has_value();
    });
    return dates >= 3;
}

} // namespace fdss::core
