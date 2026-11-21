#include "core/parsing/SquadImport.hpp"

#include "core/domain/Validation.hpp"
#include "core/parsing/HeaderMatching.hpp"
#include "core/parsing/PositionSynonyms.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <map>
#include <utility>

namespace fdss::core {

namespace {

constexpr std::size_t kFieldCount = kSquadFields.size();

// Folded (foldText).
constexpr std::array<std::string_view, 7> kFirstName = {
    "imie", "imiona", "imie zawodnika", "first name", "firstname", "given name", "forename"};
constexpr std::array<std::string_view, 6> kLastName = {
    "nazwisko", "nazwisko zawodnika", "last name", "lastname", "surname", "family name"};
constexpr std::array<std::string_view, 20> kFullName = {"imie i nazwisko",
                                                        "nazwisko i imie",
                                                        "imie nazwisko",
                                                        "nazwisko imie",
                                                        "imie oraz nazwisko",
                                                        "nazwisko oraz imie",
                                                        "zawodnik",
                                                        "pilkarz",
                                                        "gracz",
                                                        "name",
                                                        "full name",
                                                        "fullname",
                                                        "player",
                                                        "player name",
                                                        "name surname",
                                                        "surname name",
                                                        "first name last name",
                                                        "last name first name",
                                                        "name and surname",
                                                        "surname and name"};
constexpr std::array<std::string_view, 16> kShirtNumber = {"nr",
                                                           "numer",
                                                           "nr koszulki",
                                                           "numer koszulki",
                                                           "nr na koszulce",
                                                           "numer na koszulce",
                                                           "number",
                                                           "no",
                                                           "shirt",
                                                           "shirt number",
                                                           "shirt no",
                                                           "squad number",
                                                           "jersey",
                                                           "jersey number",
                                                           "kit number",
                                                           "#"};
constexpr std::array<std::string_view, 16> kBirthYear = {
    "rok urodzenia",  "rok ur",  "r ur",       "rocznik",       "ur",  "urodzony",
    "data urodzenia", "data ur", "birth year", "year of birth", "yob", "born",
    "date of birth",  "dob",     "birth date", "birthdate"};
constexpr std::array<std::string_view, 9> kPrimaryPosition = {
    "pozycja",  "poz", "pozycja glowna",   "pozycja podstawowa", "glowna pozycja",
    "position", "pos", "primary position", "main position"};
constexpr std::array<std::string_view, 9> kSecondaryPositions = {
    "pozycje dodatkowe", "pozycja dodatkowa",   "inne pozycje",
    "dodatkowe pozycje", "secondary positions", "secondary position",
    "other positions",   "alt positions",       "alternative positions"};
constexpr std::array<std::string_view, 10> kFoot = {
    "noga", "lepsza noga",    "noga wiodaca", "wiodaca noga",   "preferowana noga",
    "foot", "preferred foot", "strong foot",  "strongest foot", "footedness"};

// FullName first, so "Imię i nazwisko" is never taken for a first name.
const std::array<FieldSynonyms, kFieldCount> kSynonyms = {
    FieldSynonyms{static_cast<int>(SquadField::FullName), kFullName},
    FieldSynonyms{static_cast<int>(SquadField::FirstName), kFirstName},
    FieldSynonyms{static_cast<int>(SquadField::LastName), kLastName},
    FieldSynonyms{static_cast<int>(SquadField::ShirtNumber), kShirtNumber},
    FieldSynonyms{static_cast<int>(SquadField::BirthYear), kBirthYear},
    FieldSynonyms{static_cast<int>(SquadField::PrimaryPosition), kPrimaryPosition},
    FieldSynonyms{static_cast<int>(SquadField::SecondaryPositions), kSecondaryPositions},
    FieldSynonyms{static_cast<int>(SquadField::Foot), kFoot},
};

constexpr std::array<std::string_view, kFieldCount> kFieldKeys = {
    "first_name", "last_name",        "full_name",           "shirt_number",
    "birth_year", "primary_position", "secondary_positions", "foot"};

constexpr std::array<std::string_view, 4> kLastFirstPrefixes = {"nazwisko", "surname", "last name",
                                                                "family name"};

using ColumnOf = std::array<std::optional<std::size_t>, kFieldCount>;

std::size_t indexOf(SquadField field) { return static_cast<std::size_t>(field); }

ColumnOf columnsOf(const SquadMapping& mapping) {
    ColumnOf columns;
    for (std::size_t column = 0; column < mapping.columns.size(); ++column) {
        if (const auto field = mapping.columns[column]; field && !columns[indexOf(*field)]) {
            columns[indexOf(*field)] = column;
        }
    }
    return columns;
}

bool isSpaceAt(std::string_view text, std::size_t index, std::size_t& length) {
    const char character = text[index];
    if (character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
        character == '\v' || character == '\f') {
        length = 1;
        return true;
    }
    // U+00A0, which spreadsheets and CP1250 files bring.
    if (character == '\xC2' && index + 1 < text.size() && text[index + 1] == '\xA0') {
        length = 2;
        return true;
    }
    return false;
}

// Trimmed, every run of whitespace one space.
std::string collapseSpaces(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t length = 0;
        if (isSpaceAt(text, index, length)) {
            pendingSpace = !out.empty();
            index += length;
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(text[index]);
        ++index;
    }
    return out;
}

std::string nameKey(std::string_view firstName, std::string_view lastName) {
    const std::string first = foldName(firstName);
    const std::string last = foldName(lastName);
    return first.empty() || last.empty() ? std::string{} : first + '\x1F' + last;
}

bool allDigits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

std::optional<int> toInt(std::string_view digits) {
    int value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

void addIssue(SquadRow& row, RowIssueKind kind, IssueSeverity severity, SquadField field,
              std::string value = {}) {
    row.issues.push_back(RowIssue{kind, severity, field, std::move(value), {}, {}});
}

bool hasIssueOn(const SquadRow& row, SquadField field) {
    return std::ranges::any_of(row.issues, [&](const RowIssue& issue) {
        return issue.field == field && issue.severity == IssueSeverity::Error;
    });
}

PlayerRef refOf(const Player& player) {
    return PlayerRef{player.id, player.firstName, player.lastName, player.shirtNumber};
}

struct RowContext {
    const SourceRow& source;
    const SquadMapping& mapping;
    const ColumnOf& columns;

    // The trimmed cell of a field; nullopt when no column has the field.
    [[nodiscard]] std::optional<std::string> cell(SquadField field) const {
        const auto column = columns[indexOf(field)];
        if (!column) {
            return std::nullopt;
        }
        return trimmed(cellAt(source, *column));
    }
};

void readName(const RowContext& context, SquadRow& row) {
    if (const auto full = context.cell(SquadField::FullName)) {
        const std::string text = collapseSpaces(*full);
        if (text.empty()) {
            addIssue(row, RowIssueKind::MissingValue, IssueSeverity::Error, SquadField::FullName);
            return;
        }
        const auto parts = splitFullName(text, context.mapping.nameOrder);
        if (!parts) {
            addIssue(row, RowIssueKind::SingleWordName, IssueSeverity::Error, SquadField::FullName,
                     text);
            return;
        }
        row.draft.firstName = parts->firstName;
        row.draft.lastName = parts->lastName;
        row.filled.name = true;
        return;
    }
    row.draft.firstName = collapseSpaces(context.cell(SquadField::FirstName).value_or(""));
    row.draft.lastName = collapseSpaces(context.cell(SquadField::LastName).value_or(""));
    if (row.draft.firstName.empty()) {
        addIssue(row, RowIssueKind::MissingValue, IssueSeverity::Error, SquadField::FirstName);
    }
    if (row.draft.lastName.empty()) {
        addIssue(row, RowIssueKind::MissingValue, IssueSeverity::Error, SquadField::LastName);
    }
    row.filled.name = !row.draft.firstName.empty() && !row.draft.lastName.empty();
}

void readNumbers(const RowContext& context, SquadRow& row) {
    if (const auto text = context.cell(SquadField::ShirtNumber); text && !text->empty()) {
        if (const auto number = parseWholeNumber(*text)) {
            row.draft.shirtNumber = number;
            row.filled.shirtNumber = true;
        } else {
            addIssue(row, RowIssueKind::NotANumber, IssueSeverity::Error, SquadField::ShirtNumber,
                     *text);
        }
    }
    if (const auto text = context.cell(SquadField::BirthYear); text && !text->empty()) {
        if (const auto year = parseBirthYear(*text)) {
            row.draft.birthYear = year;
            row.filled.birthYear = true;
        } else {
            addIssue(row, RowIssueKind::NotANumber, IssueSeverity::Error, SquadField::BirthYear,
                     *text);
        }
    }
}

void readPositions(const RowContext& context, SquadRow& row) {
    std::vector<Position> extra; // the rest of a list in the position cell
    const auto primary = context.cell(SquadField::PrimaryPosition);
    if (primary && !primary->empty()) {
        if (const auto position = parsePosition(*primary)) {
            row.draft.primaryPosition = *position;
            row.filled.primaryPosition = true;
        } else if (PositionList list = parsePositionList(*primary);
                   list.unknown.empty() && !list.positions.empty()) {
            row.draft.primaryPosition = list.positions.front();
            extra.assign(std::next(list.positions.begin()), list.positions.end());
            row.filled.primaryPosition = true;
        } else {
            addIssue(row, RowIssueKind::UnknownPosition, IssueSeverity::Error,
                     SquadField::PrimaryPosition, *primary);
        }
    } else if (context.mapping.defaultPosition) {
        row.draft.primaryPosition = *context.mapping.defaultPosition;
        addIssue(row, RowIssueKind::DefaultPosition, IssueSeverity::Warning,
                 SquadField::PrimaryPosition);
    } else {
        addIssue(row, RowIssueKind::MissingValue, IssueSeverity::Error,
                 SquadField::PrimaryPosition);
    }

    std::vector<Position> secondary = std::move(extra);
    if (const auto text = context.cell(SquadField::SecondaryPositions); text && !text->empty()) {
        PositionList list = parsePositionList(*text);
        for (std::string& unknown : list.unknown) {
            addIssue(row, RowIssueKind::UnknownPosition, IssueSeverity::Warning,
                     SquadField::SecondaryPositions, std::move(unknown));
        }
        secondary.insert(secondary.end(), list.positions.begin(), list.positions.end());
    }
    std::vector<Position> unique;
    for (const Position position : secondary) {
        if (std::ranges::find(unique, position) == unique.end()) {
            unique.push_back(position);
        }
    }
    row.filled.secondaryPositions = !unique.empty();
    row.secondaryCandidates = unique;
    std::erase(unique, row.draft.primaryPosition);
    row.draft.secondaryPositions = std::move(unique);
}

void readFoot(const RowContext& context, SquadRow& row) {
    const auto text = context.cell(SquadField::Foot);
    if (!text || text->empty()) {
        return;
    }
    if (const auto foot = parseFoot(*text)) {
        row.draft.foot = *foot;
        row.filled.foot = true;
    } else {
        addIssue(row, RowIssueKind::UnknownFoot, IssueSeverity::Warning, SquadField::Foot, *text);
    }
}

// The limits of core::validate(): name lengths and the ranges of number and year.
void checkLimits(const RowContext& context, SquadRow& row, int currentYear) {
    const bool fullName = context.columns[indexOf(SquadField::FullName)].has_value();
    for (const ValidationError& error : validate(row.draft, currentYear)) {
        if (error.field == "first_name" || error.field == "last_name") {
            const SquadField field = fullName                      ? SquadField::FullName
                                     : error.field == "first_name" ? SquadField::FirstName
                                                                   : SquadField::LastName;
            if (!hasIssueOn(row, field)) {
                addIssue(row, RowIssueKind::TooLong, IssueSeverity::Error, field,
                         error.field == "first_name" ? row.draft.firstName : row.draft.lastName);
            }
        } else if (error.field == "shirt_number") {
            addIssue(row, RowIssueKind::OutOfRange, IssueSeverity::Error, SquadField::ShirtNumber,
                     context.cell(SquadField::ShirtNumber).value_or(""));
        } else if (error.field == "birth_year") {
            addIssue(row, RowIssueKind::OutOfRange, IssueSeverity::Error, SquadField::BirthYear,
                     context.cell(SquadField::BirthYear).value_or(""));
        }
    }
}

// Rows with the same key mark each other as duplicates.
void markDuplicates(std::vector<SquadRow>& rows,
                    const std::map<std::string, std::vector<std::size_t>>& groups,
                    SquadField field) {
    for (const auto& [key, members] : groups) {
        if (members.size() < 2) {
            continue;
        }
        for (const std::size_t member : members) {
            RowIssue issue{RowIssueKind::DuplicateInFile, IssueSeverity::Error, field, {}, {}, {}};
            const std::size_t other = members.front() == member ? members[1] : members.front();
            issue.otherRows.push_back(rows[other].sourceRow);
            issue.duplicateCount = static_cast<int>(members.size() - 1);
            rows[member].issues.push_back(std::move(issue));
        }
    }
}

void findDuplicates(std::vector<SquadRow>& rows, const ColumnOf& columns) {
    std::map<std::string, std::vector<std::size_t>> byNumber;
    std::map<std::string, std::vector<std::size_t>> byName;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const SquadRow& row = rows[index];
        if (row.templateExample) {
            continue;
        }
        if (row.draft.shirtNumber && !hasIssueOn(row, SquadField::ShirtNumber)) {
            byNumber[std::to_string(*row.draft.shirtNumber)].push_back(index);
        }
        if (row.filled.name) {
            const std::string key = nameKey(row.draft.firstName, row.draft.lastName);
            if (!key.empty()) {
                byName[key].push_back(index);
            }
        }
    }
    markDuplicates(rows, byNumber, SquadField::ShirtNumber);
    markDuplicates(rows, byName,
                   columns[indexOf(SquadField::FullName)] ? SquadField::FullName
                                                          : SquadField::LastName);
}

// Matching rules (packet P16 decisions): the same name, unless the number belongs to another
// player; the same number with the same last name; everything else that fits several players or a
// player with another name is ambiguous and skipped.
void matchExisting(SquadRow& row, std::span<const Player> existing, SquadField nameField) {
    const std::string key = nameKey(row.draft.firstName, row.draft.lastName);
    if (key.empty()) {
        return;
    }
    std::vector<const Player*> byName;
    const Player* byNumber = nullptr;
    for (const Player& player : existing) {
        if (nameKey(player.firstName, player.lastName) == key) {
            byName.push_back(&player);
        }
        if (row.draft.shirtNumber && player.shirtNumber == row.draft.shirtNumber) {
            byNumber = &player;
        }
    }
    const auto ambiguous = [&](SquadField field, std::vector<const Player*> candidates) {
        row.match = PlayerMatch::Ambiguous;
        RowIssue issue{RowIssueKind::AmbiguousMatch, IssueSeverity::Warning, field, {}, {}, {}};
        for (const Player* candidate : candidates) {
            issue.candidates.push_back(refOf(*candidate));
        }
        row.issues.push_back(std::move(issue));
    };
    const auto existingPlayer = [&](const Player& player) {
        row.match = PlayerMatch::Existing;
        row.existing = player.id;
    };

    if (byName.size() == 1) {
        if (byNumber != nullptr && byNumber != byName.front()) {
            existingPlayer(*byName.front());
            // An error only when the update would change the number (statusFor()).
            RowIssue issue{RowIssueKind::NumberConflict,
                           IssueSeverity::Warning,
                           SquadField::ShirtNumber,
                           std::to_string(*row.draft.shirtNumber),
                           {},
                           {refOf(*byNumber)}};
            row.issues.push_back(std::move(issue));
        } else {
            existingPlayer(*byName.front());
        }
        return;
    }
    if (byName.size() > 1) {
        if (byNumber != nullptr && std::ranges::find(byName, byNumber) != byName.end()) {
            existingPlayer(*byNumber);
        } else {
            if (byNumber != nullptr) {
                byName.push_back(byNumber);
            }
            ambiguous(nameField, std::move(byName));
        }
        return;
    }
    if (byNumber != nullptr) {
        const std::string last = foldName(row.draft.lastName);
        if (!last.empty() && foldName(byNumber->lastName) == last) {
            existingPlayer(*byNumber);
        } else {
            ambiguous(SquadField::ShirtNumber, {byNumber});
        }
    }
}

} // namespace

std::string_view toKey(SquadField field) noexcept { return kFieldKeys[indexOf(field)]; }

std::optional<SquadField> squadFieldFromKey(std::string_view key) noexcept {
    for (const SquadField field : kSquadFields) {
        if (toKey(field) == key) {
            return field;
        }
    }
    return std::nullopt;
}

std::string_view toKey(NameOrder order) noexcept {
    return order == NameOrder::FirstLast ? "first_last" : "last_first";
}

std::optional<NameOrder> nameOrderFromKey(std::string_view key) noexcept {
    if (key == "first_last") {
        return NameOrder::FirstLast;
    }
    if (key == "last_first") {
        return NameOrder::LastFirst;
    }
    return std::nullopt;
}

NameOrder detectNameOrder(std::string_view header) {
    const std::string folded = foldText(header);
    const bool lastFirst = std::ranges::any_of(
        kLastFirstPrefixes, [&](std::string_view prefix) { return folded.starts_with(prefix); });
    return lastFirst ? NameOrder::LastFirst : NameOrder::FirstLast;
}

SquadMapping autoMapSquad(std::span<const std::string> headers) {
    SquadMapping mapping;
    const std::vector<std::optional<int>> matched = matchHeaders(headers, kSynonyms);
    mapping.columns.reserve(matched.size());
    for (std::size_t column = 0; column < matched.size(); ++column) {
        if (!matched[column]) {
            mapping.columns.emplace_back();
            continue;
        }
        const auto field = static_cast<SquadField>(*matched[column]);
        mapping.columns.emplace_back(field);
        if (field == SquadField::FullName) {
            mapping.nameOrder = detectNameOrder(headers[column]);
        }
    }
    return mapping;
}

bool needsDefaultPosition(const SquadMapping& mapping, const SourceTable& table) {
    const auto column = columnsOf(mapping)[indexOf(SquadField::PrimaryPosition)];
    if (!column) {
        return true;
    }
    return std::ranges::any_of(
        table.rows, [&](const SourceRow& row) { return trimmed(cellAt(row, *column)).empty(); });
}

std::vector<MappingIssue> checkMapping(const SquadMapping& mapping, const SourceTable& table) {
    std::vector<MappingIssue> issues;
    std::array<int, kFieldCount> counts{};
    for (const auto& field : mapping.columns) {
        if (field) {
            ++counts[indexOf(*field)];
        }
    }
    if (std::ranges::any_of(counts, [](int count) { return count > 1; })) {
        issues.push_back(MappingIssue::DuplicateField);
    }
    const bool full = counts[indexOf(SquadField::FullName)] > 0;
    const bool first = counts[indexOf(SquadField::FirstName)] > 0;
    const bool last = counts[indexOf(SquadField::LastName)] > 0;
    if (full && (first || last)) {
        issues.push_back(MappingIssue::FullNameWithParts);
    } else if (!full && !(first && last)) {
        issues.push_back(MappingIssue::MissingName);
    }
    if (!mapping.defaultPosition && needsDefaultPosition(mapping, table)) {
        issues.push_back(MappingIssue::MissingDefaultPosition);
    }
    return issues;
}

std::optional<NameParts> splitFullName(std::string_view fullName, NameOrder order) {
    const std::string text = collapseSpaces(fullName);
    std::string left;
    std::string right;
    if (const std::size_t comma = text.find(','); comma != std::string::npos) {
        left = collapseSpaces(std::string_view(text).substr(0, comma));
        right = collapseSpaces(std::string_view(text).substr(comma + 1));
    } else {
        const std::size_t space = order == NameOrder::FirstLast ? text.rfind(' ') : text.find(' ');
        if (space == std::string::npos) {
            return std::nullopt;
        }
        left = text.substr(0, space);
        right = text.substr(space + 1);
    }
    if (left.empty() || right.empty()) {
        return std::nullopt;
    }
    if (order == NameOrder::FirstLast) {
        return NameParts{std::move(left), std::move(right)};
    }
    return NameParts{std::move(right), std::move(left)};
}

RowStatus SquadRow::status() const noexcept {
    RowStatus status = RowStatus::Ok;
    for (const RowIssue& issue : issues) {
        if (issue.severity == IssueSeverity::Error) {
            return RowStatus::Error;
        }
        status = RowStatus::Warning;
    }
    return status;
}

std::vector<SquadRow> validateSquad(const SourceTable& table, const SquadMapping& mapping,
                                    std::span<const Player> existing, int currentYear,
                                    const std::function<void(std::size_t)>& progress) {
    const ColumnOf columns = columnsOf(mapping);
    std::vector<SquadRow> rows;
    rows.reserve(table.rows.size());
    for (const SourceRow& source : table.rows) {
        const RowContext context{source, mapping, columns};
        SquadRow row;
        row.sourceRow = source.number;
        readName(context, row);
        readNumbers(context, row);
        readPositions(context, row);
        readFoot(context, row);
        checkLimits(context, row, currentYear);
        if (row.status() == RowStatus::Ok && isTemplateExample(row.draft)) {
            row.templateExample = true;
            addIssue(row, RowIssueKind::TemplateExample, IssueSeverity::Warning,
                     columns[indexOf(SquadField::FullName)] ? SquadField::FullName
                                                            : SquadField::LastName);
        }
        rows.push_back(std::move(row));
        if (progress) {
            progress(rows.size());
        }
    }
    findDuplicates(rows, columns);
    const SquadField nameField =
        columns[indexOf(SquadField::FullName)] ? SquadField::FullName : SquadField::LastName;
    for (SquadRow& row : rows) {
        if (row.status() != RowStatus::Error && !row.templateExample) {
            matchExisting(row, existing, nameField);
        }
    }
    return rows;
}

std::size_t recheckSquadMatches(std::span<SquadRow> rows, const SquadMapping& mapping,
                                std::span<const Player> existing) {
    const SquadField nameField = columnsOf(mapping)[indexOf(SquadField::FullName)]
                                     ? SquadField::FullName
                                     : SquadField::LastName;
    const auto hasConflict = [](const SquadRow& row) {
        return std::ranges::any_of(row.issues, [](const RowIssue& issue) {
            return issue.kind == RowIssueKind::NumberConflict;
        });
    };
    std::size_t changed = 0;
    for (SquadRow& row : rows) {
        if (row.templateExample) {
            continue;
        }
        const PlayerMatch oldMatch = row.match;
        const auto oldId = row.existing;
        const bool oldConflict = hasConflict(row);
        std::erase_if(row.issues, [](const RowIssue& issue) {
            return issue.kind == RowIssueKind::AmbiguousMatch ||
                   issue.kind == RowIssueKind::NumberConflict ||
                   issue.kind == RowIssueKind::MatchChanged;
        });
        row.match = PlayerMatch::New;
        row.existing.reset();
        if (row.status() == RowStatus::Error) {
            continue;
        }
        matchExisting(row, existing, nameField);
        if (row.match != oldMatch || row.existing != oldId || hasConflict(row) != oldConflict) {
            addIssue(row, RowIssueKind::MatchChanged, IssueSeverity::Warning, nameField);
            ++changed;
        }
    }
    return changed;
}

RowStatus statusFor(const SquadRow& row, ExistingPolicy policy) noexcept {
    const RowStatus status = row.status();
    if (policy == ExistingPolicy::Update && row.match == PlayerMatch::Existing &&
        std::ranges::any_of(row.issues, [](const RowIssue& issue) {
            return issue.kind == RowIssueKind::NumberConflict;
        })) {
        return RowStatus::Error;
    }
    return status;
}

RowAction actionFor(const SquadRow& row, ExistingPolicy policy) noexcept {
    if (row.templateExample) {
        return RowAction::SkipExample;
    }
    if (statusFor(row, policy) == RowStatus::Error) {
        return RowAction::Invalid;
    }
    switch (row.match) {
    case PlayerMatch::Ambiguous:
        return RowAction::SkipAmbiguous;
    case PlayerMatch::Existing:
        return policy == ExistingPolicy::Update ? RowAction::Update : RowAction::SkipExisting;
    case PlayerMatch::New:
        break;
    }
    return RowAction::Create;
}

bool isTemplateExample(const PlayerDraft& draft) {
    return draft.firstName == "Jan" && draft.lastName == "Przykładowy" && draft.shirtNumber == 10 &&
           draft.birthYear == 1999 && draft.primaryPosition == Position::Cm &&
           draft.secondaryPositions == std::vector<Position>{Position::Cb} &&
           draft.foot == PreferredFoot::Right;
}

bool looksLikeSquadTable(const SourceTable& table) {
    const SquadMapping mapping = autoMapSquad(table.headers);
    if (std::ranges::any_of(mapping.columns, [](const auto& field) { return field.has_value(); })) {
        return true;
    }
    const std::size_t width = table.headers.size();
    if (width < 2 || table.rows.empty()) {
        return false;
    }
    const auto consistent = std::ranges::count_if(
        table.rows, [width](const SourceRow& row) { return row.cells.size() == width; });
    return static_cast<std::size_t>(consistent) * 2 > table.rows.size();
}

PlayerDraft mergeUpdate(const Player& existing, const SquadRow& row) {
    PlayerDraft draft = existing;
    if (row.filled.name) {
        draft.firstName = row.draft.firstName;
        draft.lastName = row.draft.lastName;
    }
    if (row.filled.shirtNumber) {
        draft.shirtNumber = row.draft.shirtNumber;
    }
    if (row.filled.birthYear) {
        draft.birthYear = row.draft.birthYear;
    }
    if (row.filled.primaryPosition) {
        draft.primaryPosition = row.draft.primaryPosition;
    }
    if (row.filled.secondaryPositions) {
        draft.secondaryPositions = row.secondaryCandidates;
    }
    if (row.filled.foot) {
        draft.foot = row.draft.foot;
    }
    std::erase(draft.secondaryPositions, draft.primaryPosition);
    return draft;
}

std::optional<int> parseWholeNumber(std::string_view text) {
    const std::string value = trimmed(text);
    std::string_view digits = value;
    if (const std::size_t separator = digits.find_first_of(".,");
        separator != std::string_view::npos) {
        const std::string_view fraction = digits.substr(separator + 1);
        if (!allDigits(fraction) ||
            !std::ranges::all_of(fraction, [](char c) { return c == '0'; })) {
            return std::nullopt;
        }
        digits = digits.substr(0, separator);
    }
    // Nine digits fit an int; more is not a shirt number or a year anyway.
    if (!allDigits(digits) || digits.size() > 9) {
        return std::nullopt;
    }
    return toInt(digits);
}

std::optional<int> parseBirthYear(std::string_view text) {
    if (const auto whole = parseWholeNumber(text)) {
        return whole;
    }
    std::string value = trimmed(text);
    if (const std::size_t time = value.find_first_of("T "); time != std::string::npos) {
        value.resize(time);
    }
    std::vector<std::string_view> parts;
    std::string_view rest = value;
    while (true) {
        const std::size_t separator = rest.find_first_of("-./");
        parts.push_back(rest.substr(0, separator));
        if (separator == std::string_view::npos) {
            break;
        }
        rest = rest.substr(separator + 1);
    }
    if (parts.size() != 3 || !std::ranges::all_of(parts, allDigits)) {
        return std::nullopt;
    }
    std::string_view year;
    std::string_view first;
    std::string_view second;
    if (parts[0].size() == 4 && parts[1].size() <= 2 && parts[2].size() <= 2) {
        year = parts[0];
        first = parts[1]; // month
        second = parts[2];
    } else if (parts[2].size() == 4 && parts[0].size() <= 2 && parts[1].size() <= 2) {
        year = parts[2];
        first = parts[0];
        second = parts[1];
    } else {
        return std::nullopt;
    }
    const auto a = toInt(first);
    const auto b = toInt(second);
    const auto y = toInt(year);
    if (!a || !b || !y) {
        return std::nullopt;
    }
    const auto valid = [&](int month, int day) {
        return std::chrono::year_month_day{std::chrono::year{*y},
                                           std::chrono::month{static_cast<unsigned>(month)},
                                           std::chrono::day{static_cast<unsigned>(day)}}
            .ok();
    };
    // ISO is unambiguously year-month-day; local dates may be day-month or month-day.
    const bool isIso = parts[0].size() == 4;
    if (isIso ? !valid(*a, *b) : (!valid(*b, *a) && !valid(*a, *b))) {
        return std::nullopt;
    }
    return y;
}

} // namespace fdss::core
