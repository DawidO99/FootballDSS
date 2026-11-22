#pragma once

#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Metric.hpp"
#include "core/parsing/HeaderMatching.hpp"
#include "core/parsing/PlayerMatching.hpp"
#include "core/parsing/SourceTable.hpp"
#include "core/parsing/SquadImport.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What the training and the match import share (packet P18): the mapping of a column to a field
// or to a custom metric, the issues of a row, the player of a row and the new players an import
// creates. Pure: the service reads the database, the viewmodel turns the issues into text.

namespace fdss::core {

// The limits of core::validate() (Validation.cpp), checked per row so the report names the cell.
inline constexpr std::size_t kMaxPersonNameLength = 50;
inline constexpr std::size_t kMaxOpponentNameLength = 100;
inline constexpr int kMinRating = 1;
inline constexpr int kMaxRating = 10;

// One column: a field of the import type (TrainingField or MatchField as int), a custom metric, or
// nothing (ignored).
struct ColumnTarget {
    std::optional<int> field;
    std::optional<MetricDefinitionId> metric;

    friend bool operator==(const ColumnTarget&, const ColumnTarget&) = default;
};

struct ActivityMapping {
    std::vector<ColumnTarget> columns; // per column of the table

    friend bool operator==(const ActivityMapping&, const ActivityMapping&) = default;
};

// The metric of a header: the folded header equals the folded Polish or English name, or the key.
[[nodiscard]] std::optional<MetricDefinitionId>
metricForHeader(std::string_view header, std::span<const MetricDefinition> metrics);

// Fields by the synonyms of the headers; a column without a field gets the metric of its header
// (metricForHeader()), each metric on one column at most.
[[nodiscard]] ActivityMapping autoMapActivity(std::span<const std::string> headers,
                                              std::span<const FieldSynonyms> fields,
                                              std::span<const MetricDefinition> metrics);

// The column of every field index 0 … fieldCount - 1 (the first one, should there be more).
[[nodiscard]] std::vector<std::optional<std::size_t>> columnsOf(const ActivityMapping& mapping,
                                                                std::size_t fieldCount);

enum class ActivityMappingIssue {
    MissingDate,     // no date column
    MissingPlayer,   // no player or shirt-number column
    MissingOpponent, // matches: no opponent column
    MissingValues,   // no column with something to import (training: status, RPE, minutes or a
                     // metric; matches: started, minutes, rating, RPE or a metric)
    DuplicateField,  // one field on two columns
    DuplicateMetric, // one metric on two columns
};

enum class ActivityIssueKind {
    MissingValue,          // error: an empty required cell (date, player, opponent)
    NotADate,              // error
    NotATime,              // error
    NotANumber,            // error (a warning for a column that is not imported)
    NotAWholeNumber,       // error: a decimal where a whole number is expected (rating, RPE)
    OutOfRange,            // error
    UnknownValue,          // error: status, venue or yes/no not recognised; warning for a type
    TooLong,               // error: an opponent or competition name above its limit
    DuplicateInFile,       // error: the same player twice in one session or match (otherRows)
    Inconsistent,          // error: the rows of a session or match disagree (otherRows)
    ValuesDropped,         // warning: RPE/minutes (training) or rating/RPE (bench) left out
    NothingToImport,       // warning: no status, RPE, minutes or metric; skipped
    TemplateExample,       // warning: the example row of the template; skipped
    StartedWithoutMinutes, // error: started without minutes
    EventsNotImported,     // warning: goals, assists or cards without a minute (packet P18
                           // decision 2); `count` = goals, `count2` = assists, `count3` = cards
    VenueDefault,          // warning: no venue for the match, stored as home
    // The plan against the database (planTraining(), planMatches()):
    UnknownPlayer,      // warning: not in the team, skipped (UnknownPlayerPolicy::Skip)
    NewPlayer,          // warning: not in the team, created (UnknownPlayerPolicy::Create)
    CannotCreatePlayer, // error: an unknown player given by a number or one word only
    AmbiguousPlayer,    // warning: several players fit (candidates); skipped
    SeveralSessions,    // error: several sessions that day and no start time in the file
    SeveralMatches,     // error: several matches against the opponent that day
    MatchRecorded,      // error: the match was run in Match Day (or is live)
    MatchPlanned,       // error: the match is still planned
    ScoreDiffers,       // warning: the stored score stays (value = the stored score "2:1")
    ExistingKept,       // warning: the database holds other values; kept (update is off)
    PlanChanged,        // warning: the player of the row is another one since the check (the
                        // squad changed); the plan shows the new match
    MetricChanged,      // error: the metric of the column was deleted or changed its scope,
                        // type or limits since the check, so the value does not fit
};

struct ActivityIssue {
    ActivityIssueKind kind = ActivityIssueKind::MissingValue;
    IssueSeverity severity = IssueSeverity::Error;
    std::string field;          // a field key ("date", "rpe"…) or "metric:<key>"; may be empty
    std::string value;          // the cell as written, trimmed
    std::vector<int> otherRows; // DuplicateInFile, Inconsistent: other rows of the file
    std::vector<PlayerRef> candidates; // AmbiguousPlayer
    int count = 0;
    int count2 = 0;
    int count3 = 0;

    friend bool operator==(const ActivityIssue&, const ActivityIssue&) = default;
};

// DuplicateField and DuplicateMetric of a mapping.
[[nodiscard]] std::vector<ActivityMappingIssue> duplicateIssues(const ActivityMapping& mapping);

[[nodiscard]] inline ActivityIssue makeIssue(ActivityIssueKind kind, IssueSeverity severity,
                                             std::string field = {}, std::string value = {},
                                             std::vector<int> otherRows = {},
                                             std::vector<PlayerRef> candidates = {}) {
    ActivityIssue issue;
    issue.kind = kind;
    issue.severity = severity;
    issue.field = std::move(field);
    issue.value = std::move(value);
    issue.otherRows = std::move(otherRows);
    issue.candidates = std::move(candidates);
    return issue;
}

[[nodiscard]] RowStatus statusOf(std::span<const ActivityIssue> issues) noexcept;
[[nodiscard]] bool hasErrors(std::span<const ActivityIssue> issues) noexcept;

// The player cell of a row and what matchPlayer() made of it.
struct RowPlayer {
    std::string text; // the player cell, trimmed
    std::optional<int> shirtNumber;
    PlayerLookup lookup;

    friend bool operator==(const RowPlayer&, const RowPlayer&) = default;
};

struct MetricCell {
    MetricDefinitionId metric;
    std::string text; // as written
    double value = 0;

    friend bool operator==(const MetricCell&, const MetricCell&) = default;
};

enum class UnknownPlayerPolicy { Skip, Create };

// What the plan does with the existing database (packet P18 decisions 3, 8).
struct ActivityOptions {
    UnknownPlayerPolicy unknownPlayers = UnknownPlayerPolicy::Skip;
    bool updateExisting = false;
    NameOrder nameOrder = NameOrder::FirstLast;
    std::optional<Position> defaultPosition; // needed to create players
    bool skipInvalid = true;

    friend bool operator==(const ActivityOptions&, const ActivityOptions&) = default;
};

// A player the import creates. Several rows of the same name share one.
struct NewPlayer {
    std::string key; // personKey()
    PlayerDraft draft;
    std::vector<int> rows; // source rows

    friend bool operator==(const NewPlayer&, const NewPlayer&) = default;
};

// What the plan does with a row.
enum class ActivityAction {
    Create,       // something new is written (attendance, appearance or a metric value)
    Update,       // existing values are overwritten (update is on)
    Unchanged,    // the database already holds the same values
    KeepExisting, // the database holds other values; kept
    Skip,         // nothing is written: an unknown or ambiguous player, an example, nothing given
    Invalid,      // errors
};

// What happens with one value of a row.
enum class WriteKind { None, Create, Update, Unchanged, Keep };

struct PlannedValue {
    MetricDefinitionId metric;
    double value = 0;
    WriteKind write = WriteKind::None;
    std::optional<double> previous; // Update, Keep: the stored value

    friend bool operator==(const PlannedValue&, const PlannedValue&) = default;
};

// The player of a planned row: an existing one or one of the new players of the plan.
struct PlannedPlayer {
    std::optional<PlayerId> existing;
    std::optional<std::size_t> newPlayer; // index into the new players of the plan

    friend bool operator==(const PlannedPlayer&, const PlannedPlayer&) = default;
};

// The new player of an unknown row under UnknownPlayerPolicy::Create: the name split by the order,
// the shirt number unless another player holds it. A number or one word only: nullopt.
[[nodiscard]] std::optional<PlayerDraft> newPlayerDraft(const RowPlayer& player,
                                                        const ActivityOptions& options,
                                                        std::span<const Player> existing);

// Adds the player of a row to the plan: PlannedPlayer, or an issue that skips the row (Unknown,
// Ambiguous) or makes it invalid (CannotCreatePlayer). `newPlayers` collects the created ones.
struct PlayerResolution {
    std::optional<PlannedPlayer> player;
    std::optional<ActivityIssue> issue;
};
[[nodiscard]] PlayerResolution resolvePlayer(const RowPlayer& player, int sourceRow,
                                             const ActivityOptions& options,
                                             std::span<const Player> existing,
                                             std::vector<NewPlayer>& newPlayers);

// The player of a row against the squad now (review of P18): matchPlayer() again, so a squad
// changed after the check (numbers swapped, a player added) gives the plan of now. A match other
// than the one of the validation adds PlanChanged to `issues`.
[[nodiscard]] RowPlayer currentPlayer(const RowPlayer& validated, std::span<const Player> players,
                                      std::vector<ActivityIssue>& issues);

// The metric cells of a row against the definitions now (review of P18): each text is read again
// with its metric; a metric that is gone (or has another scope, so it is not in `metrics`) or no
// longer takes the value adds MetricChanged to `issues` and leaves the cell out.
[[nodiscard]] std::vector<MetricCell> currentMetrics(std::span<const MetricCell> cells,
                                                     std::span<const MetricDefinition> metrics,
                                                     std::vector<ActivityIssue>& issues);

// The definitions the cells of `rows` name, as they are now: part of a plan, so a definition
// changed after the check changes the plan.
template <class Row>
[[nodiscard]] std::vector<MetricDefinition>
metricsOfRows(const std::vector<Row>& rows, std::span<const MetricDefinition> metrics) {
    std::vector<MetricDefinition> used;
    for (const MetricDefinition& metric : metrics) {
        const bool named = std::ranges::any_of(rows, [&](const Row& row) {
            return std::ranges::any_of(
                row.metrics, [&](const MetricCell& cell) { return cell.metric == metric.id; });
        });
        if (named) {
            used.push_back(metric);
        }
    }
    return used;
}

// The action of a row from what happens with its values (the most significant one).
[[nodiscard]] ActivityAction combineWrites(std::span<const WriteKind> writes) noexcept;

// "12", "12.0"; a decimal ("6,5") is NotAWholeNumber, anything else NotANumber. Range checked.
struct WholeNumberCell {
    std::optional<int> value;
    std::optional<ActivityIssueKind> issue;
};
[[nodiscard]] WholeNumberCell readWholeNumber(std::string_view text, int min, int max);

// An error of the row on that field.
[[nodiscard]] bool hasErrorOn(std::span<const ActivityIssue> issues, std::string_view field);

// Rows of one session or match that disagree on a value (packet P18 decision 6, review): every
// row of the group gets an Inconsistent error naming a row with another value, so no row of the
// group is written. `agreed` is the value when they agree, otherwise empty.
template <class Row, class Value>
void markInconsistent(std::vector<Row>& rows, std::span<const std::size_t> group,
                      std::optional<Value> Row::*member, std::string_view field,
                      std::optional<Value>& agreed) {
    std::optional<std::size_t> first;
    std::optional<std::size_t> other; // the first row with a value other than `first`
    for (const std::size_t index : group) {
        const auto& value = rows[index].*member;
        if (!value) {
            continue;
        }
        if (!first) {
            first = index;
            agreed = value;
        } else if (!other && *value != *(rows[*first].*member)) {
            other = index;
        }
    }
    if (!other) {
        return;
    }
    agreed.reset();
    for (const std::size_t index : group) {
        const auto& value = rows[index].*member;
        // A row names a row with another value; a row without a value names the first one.
        const std::size_t named = value && *value == *(rows[*first].*member) ? *other : *first;
        rows[index].issues.push_back(makeIssue(ActivityIssueKind::Inconsistent,
                                               IssueSeverity::Error, std::string(field), {},
                                               {rows[named].sourceRow}));
    }
}

// What makes two rows the same player of the file: the player found, or the text (or number) of an
// unknown one. Empty for an ambiguous one, which is never a duplicate.
[[nodiscard]] std::string duplicatePlayerKey(const RowPlayer& player);

// The same player twice in one session or match: both rows get DuplicateInFile naming the other.
// Rows for which `ignored` holds are left out.
template <class Row, class Ignored>
void markDuplicatePlayers(std::vector<Row>& rows, std::span<const std::size_t> group,
                          Ignored ignored) {
    std::vector<std::pair<std::string, std::size_t>> seen;
    for (const std::size_t index : group) {
        Row& row = rows[index];
        const std::string key = ignored(row) ? std::string{} : duplicatePlayerKey(row.player);
        if (key.empty()) {
            continue;
        }
        const auto it = std::ranges::find(seen, key, &std::pair<std::string, std::size_t>::first);
        if (it == seen.end()) {
            seen.emplace_back(key, index);
            continue;
        }
        Row& other = rows[it->second];
        row.issues.push_back(makeIssue(ActivityIssueKind::DuplicateInFile, IssueSeverity::Error,
                                       "player", row.player.text, {other.sourceRow}));
        if (std::ranges::none_of(other.issues, [](const ActivityIssue& issue) {
                return issue.kind == ActivityIssueKind::DuplicateInFile;
            })) {
            other.issues.push_back(makeIssue(ActivityIssueKind::DuplicateInFile,
                                             IssueSeverity::Error, "player", other.player.text,
                                             {row.sourceRow}));
        }
    }
}

// The example row of the training and match templates (ui/import-templates): the player
// "Jan Przykładowy", as in the squad template. Such a row is skipped with a note.
[[nodiscard]] bool isTemplateExamplePlayer(std::string_view playerCell);

} // namespace fdss::core
