#include "core/parsing/HeaderMatching.hpp"

#include "core/parsing/TextEncoding.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace fdss::core {

namespace {

// U+00C0-U+00FF and U+0100-U+017F as ASCII letters; ' ' for × and ÷.
constexpr std::string_view kLatin1 = "aaaaaaaceeeeiiiidnooooo ouuuuyts"
                                     "aaaaaaaceeeeiiiidnooooo ouuuuyty";
constexpr std::string_view kLatinExtendedA = "aaaaaaccccccccdd"
                                             "ddeeeeeeeeeegggg"
                                             "gggghhhhiiiiiiii"
                                             "iiiijjkkklllllll"
                                             "lllnnnnnnnnnoooo"
                                             "oooorrrrrrssssss"
                                             "ssttttttuuuuuuuu"
                                             "uuuuwwyyyzzzzzzs";
static_assert(kLatin1.size() == 0x40 && kLatinExtendedA.size() == 0x80);

// The next code point; invalid UTF-8 counts as one U+FFFD per byte.
char32_t nextCodePoint(std::string_view text, std::size_t& index) {
    const auto byte = [&](std::size_t at) { return static_cast<std::uint8_t>(text[at]); };
    const std::uint8_t lead = byte(index);
    std::size_t length = 1;
    char32_t value = lead;
    if (lead >= 0xC0 && lead < 0xE0) {
        length = 2;
        value = lead & 0x1F;
    } else if (lead >= 0xE0 && lead < 0xF0) {
        length = 3;
        value = lead & 0x0F;
    } else if (lead >= 0xF0 && lead < 0xF8) {
        length = 4;
        value = lead & 0x07;
    } else if (lead >= 0x80) {
        ++index;
        return 0xFFFD;
    }
    if (index + length > text.size()) {
        ++index;
        return 0xFFFD;
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
        if ((byte(index + offset) & 0xC0) != 0x80) {
            ++index;
            return 0xFFFD;
        }
        value = (value << 6) | (byte(index + offset) & 0x3F);
    }
    index += length;
    return value;
}

// The folded character, or ' ' for a separator.
char fold(char32_t codePoint) {
    if (codePoint >= 'A' && codePoint <= 'Z') {
        return static_cast<char>(codePoint - 'A' + 'a');
    }
    if ((codePoint >= 'a' && codePoint <= 'z') || (codePoint >= '0' && codePoint <= '9') ||
        codePoint == '#') {
        return static_cast<char>(codePoint);
    }
    if (codePoint >= 0xC0 && codePoint <= 0xFF) {
        return kLatin1[codePoint - 0xC0];
    }
    if (codePoint >= 0x100 && codePoint <= 0x17F) {
        return kLatinExtendedA[codePoint - 0x100];
    }
    return ' ';
}

} // namespace

std::string foldText(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    bool pendingSpace = false;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const char folded = fold(nextCodePoint(utf8, index));
        if (folded == ' ') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(folded);
    }
    return out;
}

std::string foldName(std::string_view utf8) {
    std::string out;
    bool pendingSpace = false;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const char32_t point = nextCodePoint(utf8, index);
        const char latin = fold(point);
        if (point <= 0x17F && latin == ' ') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        if (point <= 0x17F) {
            out.push_back(latin);
        } else {
            appendUtf8(out, point);
        }
    }
    return out;
}

std::vector<std::optional<int>> matchHeaders(std::span<const std::string> headers,
                                             std::span<const FieldSynonyms> fields) {
    std::vector<std::optional<int>> result(headers.size());
    std::vector<int> taken;
    for (std::size_t column = 0; column < headers.size(); ++column) {
        const std::string folded = foldText(headers[column]);
        if (folded.empty()) {
            continue;
        }
        for (const FieldSynonyms& field : fields) {
            if (std::ranges::find(taken, field.field) != taken.end()) {
                continue;
            }
            if (std::ranges::find(field.names, std::string_view(folded)) != field.names.end()) {
                result[column] = field.field;
                taken.push_back(field.field);
                break;
            }
        }
    }
    return result;
}

std::string headerKey(std::span<const std::string> headers) {
    std::string key;
    for (std::size_t column = 0; column < headers.size(); ++column) {
        if (column > 0) {
            key.push_back('|');
        }
        key += foldText(headers[column]);
    }
    return key;
}

} // namespace fdss::core
