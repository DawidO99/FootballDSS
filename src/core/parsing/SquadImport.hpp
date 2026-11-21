#pragma once

#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Player.hpp"
#include "core/parsing/SourceTable.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Squad import (packet P16): mapping of the columns, validation of every row and matching with the
// players already in the team. Pure: the service reads the players and the year, the viewmodel
// turns the issues into text.

namespace fdss::core {

enum class SquadField {
    FirstName,
    LastName,
    FullName,
    ShirtNumber,
    BirthYear,
    PrimaryPosition,
    SecondaryPositions,
    Foot
};
inline constexpr std::array kSquadFields = {
    SquadField::FirstName,          SquadField::LastName,  SquadField::FullName,
    SquadField::ShirtNumber,        SquadField::BirthYear, SquadField::PrimaryPosition,
    SquadField::SecondaryPositions, SquadField::Foot};

// Stable keys ("first_name", "full_name"…) for QML and the stored mapping.
[[nodiscard]] std::string_view toKey(SquadField field) noexcept;
[[nodiscard]] std::optional<SquadField> squadFieldFromKey(std::string_view key) noexcept;

// Order of a full-name column: "Jan Kowalski" or "Kowalski Jan".
enum class NameOrder { FirstLast, LastFirst };
[[nodiscard]] std::string_view toKey(NameOrder order) noexcept;
[[nodiscard]] std::optional<NameOrder> nameOrderFromKey(std::string_view key) noexcept;

struct SquadMapping {
    std::vector<std::optional<SquadField>> columns; // per column of the table; nullopt = ignored
    NameOrder nameOrder = NameOrder::FirstLast;
    // For rows without a position: no position column, or an empty cell.
    std::optional<Position> defaultPosition;

    friend bool operator==(const SquadMapping&, const SquadMapping&) = default;
};

// Columns by the PL/EN synonyms of their headers; the name order from the full-name header.
[[nodiscard]] SquadMapping autoMapSquad(std::span<const std::string> headers);
// "Nazwisko i imię", "Nazwisko, imię", "Surname name", "Last name, first name"… -> LastFirst.
[[nodiscard]] NameOrder detectNameOrder(std::string_view header);

enum class MappingIssue {
    MissingName,            // neither a full name nor both first and last name
    FullNameWithParts,      // a full-name column together with a first- or last-name column
    DuplicateField,         // one field on two columns
    MissingDefaultPosition, // needsDefaultPosition() without a default
};
[[nodiscard]] std::vector<MappingIssue> checkMapping(const SquadMapping& mapping,
                                                     const SourceTable& table);
// No position column, or a row with an empty position cell.
[[nodiscard]] bool needsDefaultPosition(const SquadMapping& mapping, const SourceTable& table);

struct NameParts {
    std::string firstName;
    std::string lastName;

    friend bool operator==(const NameParts&, const NameParts&) = default;
};
// Spaces collapsed. FirstLast: split at the last space; LastFirst: at the first space. A comma
// splits instead of a space ("Kowalski, Jan" with LastFirst). One word: nullopt.
[[nodiscard]] std::optional<NameParts> splitFullName(std::string_view fullName, NameOrder order);

enum class IssueSeverity { Warning, Error };

enum class RowIssueKind {
    MissingValue,    // error: an empty name
    SingleWordName,  // error: a full name without a space
    TooLong,         // error: a name above 50 characters
    NotANumber,      // error: shirt number or birth year that is not a whole number or a date
    OutOfRange,      // error: shirt number 1-99, birth year 1950 to the current year - 4
    UnknownPosition, // error for the position, warning (left out) for a secondary position
    DefaultPosition, // warning: the default position was used
    UnknownFoot,     // warning: stored as unknown
    DuplicateInFile, // error: same shirt number or same name as `otherRows`
    AmbiguousMatch,  // warning: several players of the team fit, the row is skipped
};

// A player of the team that a row could mean.
struct PlayerRef {
    PlayerId id;
    std::string firstName;
    std::string lastName;
    std::optional<int> shirtNumber;

    friend bool operator==(const PlayerRef&, const PlayerRef&) = default;
};

struct RowIssue {
    RowIssueKind kind = RowIssueKind::MissingValue;
    IssueSeverity severity = IssueSeverity::Error;
    SquadField field = SquadField::LastName;
    std::string value;                 // the cell as written, trimmed
    std::vector<int> otherRows;        // DuplicateInFile
    std::vector<PlayerRef> candidates; // AmbiguousMatch

    friend bool operator==(const RowIssue&, const RowIssue&) = default;
};

enum class RowStatus { Ok, Warning, Error };
enum class PlayerMatch { New, Existing, Ambiguous };

// Which values of the draft came from a cell. An update changes only those.
struct FilledFields {
    bool name = false;
    bool shirtNumber = false;
    bool birthYear = false;
    bool primaryPosition = false; // false when the default was used
    bool secondaryPositions = false;
    bool foot = false;

    friend bool operator==(const FilledFields&, const FilledFields&) = default;
};

struct SquadRow {
    int sourceRow = 0;
    PlayerDraft draft;
    FilledFields filled;
    std::vector<RowIssue> issues;
    PlayerMatch match = PlayerMatch::New;
    std::optional<PlayerId> existing; // PlayerMatch::Existing

    [[nodiscard]] RowStatus status() const noexcept;
};

// Every row of the table, in order. `existing`: the players of the team who are not archived.
// `progress` gets the number of rows done.
[[nodiscard]] std::vector<SquadRow>
validateSquad(const SourceTable& table, const SquadMapping& mapping,
              std::span<const Player> existing, int currentYear,
              const std::function<void(std::size_t)>& progress = {});

enum class ExistingPolicy { Update, Skip };
enum class RowAction { Create, Update, SkipExisting, SkipAmbiguous, Invalid };
[[nodiscard]] RowAction actionFor(const SquadRow& row, ExistingPolicy policy) noexcept;

// The stored player with the filled values of the row.
[[nodiscard]] PlayerDraft mergeUpdate(const Player& existing, const SquadRow& row);

// "12", "12.0", "012" (spreadsheets write whole numbers as decimals). Anything else: nullopt.
[[nodiscard]] std::optional<int> parseWholeNumber(std::string_view text);
// A whole number, or the year of a date: 2001-05-12, 12.05.2001, 12/05/2001, 2001-05-12T00:00…
[[nodiscard]] std::optional<int> parseBirthYear(std::string_view text);

} // namespace fdss::core
