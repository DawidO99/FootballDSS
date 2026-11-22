#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/parsing/SourceTable.hpp"

#include <optional>
#include <string_view>

// Cell values of the training and match imports (SPEC section 8 step 4, packet P18).

namespace fdss::core {

// The largest Excel serial day the import reads (31.12.9999 in Excel); later ones are no dates.
inline constexpr int kMaxExcelSerial = 2958465;

// dd.MM.yyyy, yyyy-MM-dd, dd/MM/yyyy (one-digit day and month too), optionally followed by a time
// ("2026-03-02T00:00", "02.03.2026 18:00"). With `excelSerial` (an XLSX sheet) a whole number is a
// serial day of the 1900 date system, as a sheet holds a date cell without a date format. Dates
// outside isSupportedDate(): nullopt.
[[nodiscard]] std::optional<Date> parseImportDate(std::string_view text, bool excelSerial);

// "18:00", "8:30", "18.00", "18:00:00". nullopt for anything else.
[[nodiscard]] std::optional<TimeOfDay> parseImportTime(std::string_view text);

// tak/nie, yes/no, t/n, y/n, 1/0, x (yes), true/false, prawda/fałsz. nullopt for anything else.
[[nodiscard]] std::optional<bool> parseYesNo(std::string_view text);

// One column per date (a sheet with the players down and the days across): at least three headers
// are dates. Such a file needs the long format of the template (packet P18 decision 7).
[[nodiscard]] bool looksLikeWideFormat(const SourceTable& table);

} // namespace fdss::core
