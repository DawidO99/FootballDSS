#pragma once

#include "core/domain/Appearance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Metric.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/Player.hpp"
#include "core/parsing/ActivityImport.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Match import (packet P18 requirement 2): one row per player per match. Validation reads the
// file alone; planMatches() compares it with the matches already in the database. Goals, assists
// and cards have no minute and are not stored (decision 2): the rows count them for the report.

namespace fdss::core {

enum class MatchField {
    Date,
    Opponent,
    Venue,
    Competition,
    Score,
    GoalsFor,
    GoalsAgainst,
    Player,
    ShirtNumber,
    Started,
    Minutes,
    Goals,
    Assists,
    YellowCards,
    RedCards,
    Rating,
    Rpe
};
inline constexpr std::array kMatchFields = {
    MatchField::Date,         MatchField::Opponent,    MatchField::Venue,
    MatchField::Competition,  MatchField::Score,       MatchField::GoalsFor,
    MatchField::GoalsAgainst, MatchField::Player,      MatchField::ShirtNumber,
    MatchField::Started,      MatchField::Minutes,     MatchField::Goals,
    MatchField::Assists,      MatchField::YellowCards, MatchField::RedCards,
    MatchField::Rating,       MatchField::Rpe};

// "date", "opponent", "venue", "competition", "score", "goals_for", "goals_against", "player",
// "shirt_number", "started", "minutes", "goals", "assists", "yellow_cards", "red_cards", "rating",
// "rpe".
[[nodiscard]] std::string_view toKey(MatchField field) noexcept;
[[nodiscard]] std::optional<MatchField> matchFieldFromKey(std::string_view key) noexcept;

// Fields by PL/EN synonyms; other columns by the names of the match metrics.
[[nodiscard]] ActivityMapping autoMapMatches(std::span<const std::string> headers,
                                             std::span<const MetricDefinition> metrics);
[[nodiscard]] std::vector<ActivityMappingIssue> checkMatchMapping(const ActivityMapping& mapping);

// dom/u siebie/home, wyjazd/away, neutralny/neutral…
[[nodiscard]] std::optional<Venue> parseVenue(std::string_view text);
// "2:1", "2-1", "2–1", "2 : 1"; each side 0-99.
[[nodiscard]] std::optional<std::pair<int, int>> parseScore(std::string_view text);

struct MatchRow {
    int sourceRow = 0;
    std::optional<Date> date;
    std::string opponent;                   // trimmed
    std::optional<std::string> opponentKey; // foldName(opponent); empty without an opponent
    std::optional<Venue> venue;
    std::optional<std::string> competition;
    std::optional<std::string> competitionKey;
    std::optional<std::pair<int, int>> score; // for, against
    RowPlayer player;
    std::optional<bool> started;
    std::optional<int> minutes;
    std::optional<int> rating;
    std::optional<int> rpe;
    int goals = 0;
    int assists = 0;
    int cards = 0; // yellow + red
    int yellowCards = 0;
    std::vector<MetricCell> metrics;
    bool templateExample = false;
    std::optional<std::size_t> match; // the match group, for a row with a date and an opponent
    std::vector<ActivityIssue> issues;

    [[nodiscard]] RowStatus status() const noexcept { return statusOf(issues); }
    friend bool operator==(const MatchRow&, const MatchRow&) = default;
};

// The rows of one match of the file: the same date and opponent.
struct MatchGroup {
    Date date{};
    std::string opponent; // as in its first row
    std::string opponentKey;
    std::optional<Venue> venue;
    std::optional<std::string> competition;
    std::optional<std::pair<int, int>> score;
    int maxMinutes = 0; // of its rows without errors
    std::vector<std::size_t> rows;

    friend bool operator==(const MatchGroup&, const MatchGroup&) = default;
};

struct MatchImport {
    std::vector<MatchRow> rows;
    std::vector<MatchGroup> matches;

    friend bool operator==(const MatchImport&, const MatchImport&) = default;
};

[[nodiscard]] MatchImport validateMatches(const SourceTable& table, const ActivityMapping& mapping,
                                          std::span<const Player> players,
                                          std::span<const MetricDefinition> metrics,
                                          bool excelSerial,
                                          const std::function<void(std::size_t)>& progress = {});

// Packet P18 decision 5: a starter plays 0 -> minutes; a substitute with minutes comes on at
// finalMinute - minutes and plays to the end; anyone else is on the bench (no minutes).
[[nodiscard]] AppearanceDraft appearanceFor(bool started, std::optional<int> minutes,
                                            int finalMinute);

// What the database holds for a match of the file's dates.
struct ExistingMatch {
    Match match;
    bool recorded = false; // has match_live (Match Day ran it)
    std::vector<Appearance> appearances;
    std::vector<MetricValue> values; // of the metrics of the mapping

    friend bool operator==(const ExistingMatch&, const ExistingMatch&) = default;
};

struct PlannedMatch {
    std::optional<MatchId> existing;
    std::optional<OpponentId> opponent;     // an existing opponent
    std::optional<std::size_t> newOpponent; // index into MatchPlan::newOpponents
    MatchDraft draft;                       // opponentId empty for a new opponent
    std::optional<int> goalsFor;
    std::optional<int> goalsAgainst;
    int finalMinute = 90;
    bool written = false;

    friend bool operator==(const PlannedMatch&, const PlannedMatch&) = default;
};

struct PlannedMatchRow {
    ActivityAction action = ActivityAction::Skip;
    std::optional<std::size_t> match; // index into MatchPlan::matches
    PlannedPlayer player;
    std::optional<AppearanceDraft> appearance; // playerId empty for a new player
    WriteKind appearanceWrite = WriteKind::None;
    std::optional<AppearanceDraft> previousAppearance; // Update, Keep
    std::vector<PlannedValue> values;
    std::vector<ActivityIssue> issues;

    friend bool operator==(const PlannedMatchRow&, const PlannedMatchRow&) = default;
};

struct MatchPlan {
    std::vector<MetricDefinition> metrics; // the definitions the cells name, as they are now
    std::vector<PlannedMatch> matches;
    std::vector<std::string> newOpponents; // names, as in the file
    std::vector<NewPlayer> newPlayers;
    std::vector<PlannedMatchRow> rows;

    friend bool operator==(const MatchPlan&, const MatchPlan&) = default;
};

// Opponents by foldName() of the name; matches by date and opponent (decision 4: a recorded or
// live match and a planned one are errors, a finished one gets the missing appearances).
// `existing`: the matches of the team on the dates of the file; `metrics`: the definitions of the
// match scope now. As planTraining(), the players and metric values are read again against them.
[[nodiscard]] MatchPlan planMatches(const MatchImport& import, std::span<const Opponent> opponents,
                                    std::span<const ExistingMatch> existing,
                                    std::span<const Player> players,
                                    std::span<const MetricDefinition> metrics,
                                    const ActivityOptions& options);

// Totals of the goals, assists and cards of the rows that are written (decision 2).
struct NotImportedEvents {
    int goals = 0;
    int assists = 0;
    int cards = 0;
    int yellowCards = 0;

    friend bool operator==(const NotImportedEvents&, const NotImportedEvents&) = default;
};
[[nodiscard]] NotImportedEvents notImportedEvents(const MatchImport& import, const MatchPlan& plan);

} // namespace fdss::core
