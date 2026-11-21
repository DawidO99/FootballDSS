#include "core/parsing/TextEncoding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fdss::core {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

// Bytes 0x80-0xFF of Windows-1250 (unicode.org, CP1250.TXT). 0 marks a byte without a character.
constexpr std::array<char16_t, 128> kCp1250High = {
    0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80
    0x0000, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179, // 0x88
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90
    0x0000, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A, // 0x98
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, // 0xA0
    0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B, // 0xA8
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, // 0xB0
    0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C, // 0xB8
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, // 0xC0
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E, // 0xC8
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, // 0xD0
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF, // 0xD8
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, // 0xE0
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F, // 0xE8
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, // 0xF0
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9, // 0xF8
};

std::uint8_t byteAt(std::string_view bytes, std::size_t index) noexcept {
    return static_cast<std::uint8_t>(bytes[index]);
}

bool startsWith(std::string_view bytes, std::string_view prefix) noexcept {
    return bytes.substr(0, prefix.size()) == prefix;
}

// UTF-16 after its BOM; an unpaired surrogate becomes U+FFFD, an odd last byte is dropped.
std::string decodeUtf16(std::string_view bytes, bool littleEndian) {
    std::string out;
    out.reserve(bytes.size());
    const auto unit = [&](std::size_t index) -> char32_t {
        const std::uint8_t first = byteAt(bytes, index);
        const std::uint8_t second = byteAt(bytes, index + 1);
        return littleEndian ? static_cast<char32_t>(first | (second << 8))
                            : static_cast<char32_t>((first << 8) | second);
    };
    std::size_t index = 0;
    while (index + 1 < bytes.size()) {
        const char32_t high = unit(index);
        index += 2;
        if (high >= 0xD800 && high <= 0xDBFF) {
            if (index + 1 < bytes.size()) {
                const char32_t low = unit(index);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    index += 2;
                    appendUtf8(out, 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00));
                    continue;
                }
            }
            appendUtf8(out, kReplacement);
        } else if (high >= 0xDC00 && high <= 0xDFFF) {
            appendUtf8(out, kReplacement);
        } else {
            appendUtf8(out, high);
        }
    }
    return out;
}

} // namespace

void appendUtf8(std::string& out, char32_t codePoint) {
    if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
        codePoint = kReplacement;
    }
    if (codePoint < 0x80) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

bool isValidUtf8(std::string_view bytes) noexcept {
    std::size_t index = 0;
    while (index < bytes.size()) {
        const std::uint8_t lead = byteAt(bytes, index);
        std::size_t length = 0;
        char32_t codePoint = 0;
        if (lead < 0x80) {
            ++index;
            continue;
        }
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            codePoint = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            codePoint = lead & 0x0F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            codePoint = lead & 0x07;
        } else {
            return false;
        }
        if (index + length > bytes.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            const std::uint8_t next = byteAt(bytes, index + offset);
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (next & 0x3F);
        }
        // Overlong forms, surrogates and values above U+10FFFF.
        if ((length == 3 && codePoint < 0x800) || (length == 4 && codePoint < 0x10000) ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF) {
            return false;
        }
        index += length;
    }
    return true;
}

std::string decodeCp1250(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 4);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const std::uint8_t byte = byteAt(bytes, index);
        if (byte < 0x80) {
            out.push_back(static_cast<char>(byte));
            continue;
        }
        const char16_t mapped = kCp1250High[byte - 0x80];
        appendUtf8(out, mapped == 0 ? kReplacement : static_cast<char32_t>(mapped));
    }
    return out;
}

DecodedText decodeText(std::string_view bytes) {
    if (startsWith(bytes, "\xEF\xBB\xBF")) {
        const std::string_view rest = bytes.substr(3);
        // A BOM in front of CP1250 text is a broken file; the text still counts as CP1250.
        if (isValidUtf8(rest)) {
            return {std::string(rest), TextEncoding::Utf8Bom};
        }
        return {decodeCp1250(rest), TextEncoding::Cp1250};
    }
    if (startsWith(bytes, "\xFF\xFE")) {
        return {decodeUtf16(bytes.substr(2), true), TextEncoding::Utf16};
    }
    if (startsWith(bytes, "\xFE\xFF")) {
        return {decodeUtf16(bytes.substr(2), false), TextEncoding::Utf16};
    }
    if (isValidUtf8(bytes)) {
        return {std::string(bytes), TextEncoding::Utf8};
    }
    return {decodeCp1250(bytes), TextEncoding::Cp1250};
}

} // namespace fdss::core
