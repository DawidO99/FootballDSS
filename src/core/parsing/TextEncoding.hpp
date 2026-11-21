#pragma once

#include <string>
#include <string_view>

// Text files of an import (SPEC section 8 step 2): UTF-8 with or without BOM, UTF-16 with BOM
// ("Unicode text" of Excel), otherwise Windows-1250, which Polish Excel writes for "CSV". The
// CP1250 table is our own: the ICU codecs are missing on some Qt builds.

namespace fdss::core {

enum class TextEncoding { Utf8, Utf8Bom, Utf16, Cp1250 };

struct DecodedText {
    std::string utf8; // without the BOM
    TextEncoding encoding = TextEncoding::Utf8;
};

[[nodiscard]] bool isValidUtf8(std::string_view bytes) noexcept;

// Bytes 0x81, 0x83, 0x88, 0x90 and 0x98 have no character in CP1250 and become U+FFFD.
[[nodiscard]] std::string decodeCp1250(std::string_view bytes);

[[nodiscard]] DecodedText decodeText(std::string_view bytes);

// Appends the UTF-8 form of a code point; anything outside Unicode becomes U+FFFD.
void appendUtf8(std::string& out, char32_t codePoint);

} // namespace fdss::core
