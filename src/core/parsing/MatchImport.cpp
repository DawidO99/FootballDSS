#include "core/parsing/MatchImport.hpp"

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/Validation.hpp"
#include "core/parsing/HeaderMatching.hpp"
#include "core/parsing/ImportValues.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace fdss::core {

namespace {

constexpr std::size_t kFieldCount = kMatchFields.size();
constexpr int kDefaultFinalMinute = 90;
constexpr int kMaxGoals = 99;

constexpr std::array<std::string_view, kFieldCount> kFieldKeys = {
    "date",          "opponent",     "venue",        "competition", "score",   "goals_for",
    "goals_against", "player",       "shirt_number", "started",     "minutes", "goals",
    "assists",       "yellow_cards", "red_cards",    "rating",      "rpe"};

// Folded (foldText).
constexpr std::array<std::string_view, 7> kDate = {"data",       "data meczu", "dzien",      "date",
                                                   "match date", "day",        "dzien meczu"};
constexpr std::array<std::string_view, 10> kOpponent = {
    "przeciwnik", "rywal", "druzyna przeciwna", "przeciwnicy",   "opponent",
    "rival",      "vs",    "against",           "opponent team", "versus"};
constexpr std::array<std::string_view, 10> kVenue = {
    "miejsce",   "gospodarz", "dom wyjazd", "u siebie", "venue",
    "home away", "h a",       "location",   "d w",      "miejsce meczu"};
constexpr std::array<std::string_view, 7> kCompetition = {
    "rozgrywki", "liga", "turniej", "competition", "league", "tournament", "rodzaj meczu"};
constexpr std::array<std::string_view, 5> kScore = {"wynik", "rezultat", "score", "result",
                                                    "wynik meczu"};
constexpr std::array<std::string_view, 6> kGoalsFor = {"bramki zdobyte", "gole zdobyte", "bz",
                                                       "goals for",      "gf",           "scored"};
constexpr std::array<std::string_view, 6> kGoalsAgainst = {
    "bramki stracone", "gole stracone", "bs", "goals against", "ga", "conceded"};
constexpr std::array<std::string_view, 15> kPlayer = {
    "zawodnik",  "pilkarz",       "gracz",         "imie i nazwisko", "nazwisko i imie",
    "nazwisko",  "imie nazwisko", "nazwisko imie", "player",          "name",
    "full name", "player name",   "surname",       "last name",       "zawodniczka"};
constexpr std::array<std::string_view, 9> kShirtNumber = {
    "nr", "numer", "nr koszulki", "numer koszulki", "number", "no", "shirt", "shirt number", "#"};
constexpr std::array<std::string_view, 10> kStarted = {
    "podstawowy", "w podstawie", "pierwszy sklad", "od poczatku", "sklad podstawowy",
    "start",      "started",     "starter",        "starting",    "in starting xi"};
constexpr std::array<std::string_view, 7> kMinutes = {
    "minuty", "min", "minuty gry", "rozegrane minuty", "minutes", "mins", "minutes played"};
constexpr std::array<std::string_view, 5> kGoals = {"gole", "bramki", "goals", "gole zawodnika",
                                                    "bramki zawodnika"};
constexpr std::array<std::string_view, 4> kAssists = {"asysty", "asysta", "assists", "ast"};
constexpr std::array<std::string_view, 7> kYellow = {"zolte kartki", "zolta kartka", "zolte", "zk",
                                                     "yellow",       "yellow cards", "yc"};
constexpr std::array<std::string_view, 7> kRed = {
    "czerwone kartki", "czerwona kartka", "czerwone", "ck", "red", "red cards", "rc"};
constexpr std::array<std::string_view, 6> kRating = {"ocena", "nota", "rating",
                                                     "grade", "mark", "ocena zawodnika"};
constexpr std::array<std::string_view, 7> kRpe = {
    "rpe", "rpe 0 10", "wysilek", "odczuwany wysilek", "borg", "perceived exertion", "rpe cr10"};

const std::array<FieldSynonyms, kFieldCount> kSynonyms = {
    FieldSynonyms{static_cast<int>(MatchField::Date), kDate},
    FieldSynonyms{static_cast<int>(MatchField::Opponent), kOpponent},
    FieldSynonyms{static_cast<int>(MatchField::Venue), kVenue},
    FieldSynonyms{static_cast<int>(MatchField::Competition), kCompetition},
    FieldSynonyms{static_cast<int>(MatchField::Score), kScore},
    FieldSynonyms{static_cast<int>(MatchField::GoalsFor), kGoalsFor},
    FieldSynonyms{static_cast<int>(MatchField::GoalsAgainst), kGoalsAgainst},
    FieldSynonyms{static_cast<int>(MatchField::Player), kPlayer},
    FieldSynonyms{static_cast<int>(MatchField::ShirtNumber), kShirtNumber},
    FieldSynonyms{static_cast<int>(MatchField::Started), kStarted},
    FieldSynonyms{static_cast<int>(MatchField::Minutes), kMinutes},
    FieldSynonyms{static_cast<int>(MatchField::Goals), kGoals},
    FieldSynonyms{static_cast<int>(MatchField::Assists), kAssists},
    FieldSynonyms{static_cast<int>(MatchField::YellowCards), kYellow},
    FieldSynonyms{static_cast<int>(MatchField::RedCards), kRed},
    FieldSynonyms{static_cast<int>(MatchField::Rating), kRating},
    FieldSynonyms{static_cast<int>(MatchField::Rpe), kRpe},
};

struct VenueSynonyms {
    Venue venue;
    std::span<const std::string_view> names;
};
constexpr std::array<std::string_view, 9> kHome = {"dom", "u siebie", "gospodarz", "gosp",  "d",
                                                   "h",   "home",     "domowy",    "w domu"};
constexpr std::array<std::string_view, 8> kAway = {"wyjazd", "na wyjezdzie", "wyjazdowy", "w",
                                                   "a",      "away",         "gosc",      "wyj"};
constexpr std::array<std::string_view, 5> kNeutral = {"neutralny", "neutralne", "neutral",
                                                      "teren neutralny", "n"};
const std::array<VenueSynonyms, 3> kVenues = {VenueSynonyms{Venue::Home, kHome},
                                              VenueSynonyms{Venue::Away, kAway},
                                              VenueSynonyms{Venue::Neutral, kNeutral}};

std::size_t indexOf(MatchField field) { return static_cast<std::size_t>(field); }

void addIssue(MatchRow& row, ActivityIssueKind kind, IssueSeverity severity, std::string_view field,
              std::string value = {}) {
    row.issues.push_back(makeIssue(kind, severity, std::string(field), std::move(value)));
}

struct RowContext {
    const SourceRow& source;
    const std::vector<std::optional<std::size_t>>& columns;

    [[nodiscard]] std::string cell(MatchField field) const {
        const auto column = columns[indexOf(field)];
        return column ? trimmed(cellAt(source, *column)) : std::string{};
    }
};

std::optional<int> readNumberCell(MatchRow& row, const RowContext& context, MatchField field,
                                  int min, int max, IssueSeverity severity = IssueSeverity::Error) {
    const std::string text = context.cell(field);
    const WholeNumberCell cell = readWholeNumber(text, min, max);
    if (cell.issue) {
        addIssue(row, *cell.issue, severity, toKey(field), text);
    }
    return cell.value;
}

void readMetrics(MatchRow& row, const SourceRow& source, const ActivityMapping& mapping,
                 std::span<const MetricDefinition> metrics) {
    for (std::size_t column = 0; column < mapping.columns.size(); ++column) {
        const auto& metricId = mapping.columns[column].metric;
        if (!metricId) {
            continue;
        }
        const auto metric = std::ranges::find(metrics, *metricId, &MetricDefinition::id);
        const std::string text = trimmed(cellAt(source, column));
        if (metric == metrics.end() || text.empty()) {
            continue;
        }
        const std::string field = "metric:" + metric->key;
        const auto value = parseMetricValue(text, metric->valueType);
        if (!value) {
            addIssue(row, ActivityIssueKind::NotANumber, IssueSeverity::Error, field, text);
            continue;
        }
        if (!validateValue(*metric, *value).empty()) {
            addIssue(row, ActivityIssueKind::OutOfRange, IssueSeverity::Error, field, text);
            continue;
        }
        row.metrics.push_back(MetricCell{metric->id, text, *value});
    }
}

void readMatchCells(MatchRow& row, const RowContext& context, bool excelSerial) {
    const std::string date = context.cell(MatchField::Date);
    if (date.empty()) {
        addIssue(row, ActivityIssueKind::MissingValue, IssueSeverity::Error, "date");
    } else if (row.date = parseImportDate(date, excelSerial); !row.date) {
        addIssue(row, ActivityIssueKind::NotADate, IssueSeverity::Error, "date", date);
    }
    row.opponent = context.cell(MatchField::Opponent);
    if (row.opponent.empty()) {
        addIssue(row, ActivityIssueKind::MissingValue, IssueSeverity::Error, "opponent");
    } else if (characterCount(row.opponent) > kMaxOpponentNameLength) {
        addIssue(row, ActivityIssueKind::TooLong, IssueSeverity::Error, "opponent", row.opponent);
    } else {
        row.opponentKey = foldName(row.opponent);
    }
    if (const std::string venue = context.cell(MatchField::Venue); !venue.empty()) {
        if (row.venue = parseVenue(venue); !row.venue) {
            addIssue(row, ActivityIssueKind::UnknownValue, IssueSeverity::Error, "venue", venue);
        }
    }
    if (const std::string competition = context.cell(MatchField::Competition);
        !competition.empty()) {
        if (characterCount(competition) > kMaxOpponentNameLength) {
            addIssue(row, ActivityIssueKind::TooLong, IssueSeverity::Error, "competition",
                     competition);
        } else {
            row.competition = competition;
            row.competitionKey = foldText(competition);
        }
    }
    if (const std::string score = context.cell(MatchField::Score); !score.empty()) {
        if (row.score = parseScore(score); !row.score) {
            addIssue(row, ActivityIssueKind::NotANumber, IssueSeverity::Error, "score", score);
        }
    }
    const auto goalsFor = readNumberCell(row, context, MatchField::GoalsFor, 0, kMaxGoals);
    const auto goalsAgainst = readNumberCell(row, context, MatchField::GoalsAgainst, 0, kMaxGoals);
    if (!row.score && goalsFor && goalsAgainst) {
        row.score = std::pair{*goalsFor, *goalsAgainst};
    } else if (!row.score && goalsFor != goalsAgainst && (!goalsFor || !goalsAgainst) &&
               !hasErrorOn(row.issues, "goals_for") && !hasErrorOn(row.issues, "goals_against")) {
        // One side of the score without the other.
        addIssue(row, ActivityIssueKind::MissingValue, IssueSeverity::Error,
                 goalsFor ? "goals_against" : "goals_for");
    }
}

void readPlayerCells(MatchRow& row, const RowContext& context, std::span<const Player> players) {
    const std::string number = context.cell(MatchField::ShirtNumber);
    if (!number.empty()) {
        row.player.shirtNumber = parseWholeNumber(number);
        if (!row.player.shirtNumber) {
            addIssue(row, ActivityIssueKind::NotANumber, IssueSeverity::Error, "shirt_number",
                     number);
        }
    }
    if (row.player.text.empty() && !row.player.shirtNumber) {
        if (!hasErrorOn(row.issues, "shirt_number")) {
            addIssue(row, ActivityIssueKind::MissingValue, IssueSeverity::Error, "player");
        }
    } else {
        row.player.lookup = matchPlayer(row.player.text, row.player.shirtNumber, players);
    }
}

void readAppearanceCells(MatchRow& row, const RowContext& context) {
    if (const std::string started = context.cell(MatchField::Started); !started.empty()) {
        if (row.started = parseYesNo(started); !row.started) {
            addIssue(row, ActivityIssueKind::UnknownValue, IssueSeverity::Error, "started",
                     started);
        }
    }
    row.minutes = readNumberCell(row, context, MatchField::Minutes, 0, kMaxMatchMinute);
    row.rating = readNumberCell(row, context, MatchField::Rating, kMinRating, kMaxRating);
    row.rpe = readNumberCell(row, context, MatchField::Rpe, 0, kMaxRpe);
    // Not imported (decision 2): a bad cell is only a warning.
    const auto count = [&](MatchField field) {
        return readNumberCell(row, context, field, 0, kMaxGoals, IssueSeverity::Warning)
            .value_or(0);
    };
    row.goals = count(MatchField::Goals);
    row.assists = count(MatchField::Assists);
    row.yellowCards = count(MatchField::YellowCards);
    row.cards = row.yellowCards + count(MatchField::RedCards);

    if (row.started.value_or(false) && !hasErrorOn(row.issues, "minutes") &&
        row.minutes.value_or(0) == 0) {
        addIssue(row, ActivityIssueKind::StartedWithoutMinutes, IssueSeverity::Error, "minutes",
                 context.cell(MatchField::Minutes));
    }
    const bool played = row.started.value_or(false) || row.minutes.value_or(0) > 0;
    if (!played && (row.rating || row.rpe)) {
        addIssue(row, ActivityIssueKind::ValuesDropped, IssueSeverity::Warning, "minutes");
        row.rating.reset();
        row.rpe.reset();
    }
    if (row.goals + row.assists + row.cards > 0) {
        ActivityIssue issue =
            makeIssue(ActivityIssueKind::EventsNotImported, IssueSeverity::Warning, "goals");
        issue.count = row.goals;
        issue.count2 = row.assists;
        issue.count3 = row.cards;
        row.issues.push_back(std::move(issue));
    }
}

MatchRow readRow(const RowContext& context, const ActivityMapping& mapping,
                 std::span<const Player> players, std::span<const MetricDefinition> metrics,
                 bool excelSerial) {
    MatchRow row;
    row.sourceRow = context.source.number;
    row.player.text = context.cell(MatchField::Player);
    if (isTemplateExamplePlayer(row.player.text)) {
        row.templateExample = true;
        addIssue(row, ActivityIssueKind::TemplateExample, IssueSeverity::Warning, "player",
                 row.player.text);
        return row;
    }
    readMatchCells(row, context, excelSerial);
    readPlayerCells(row, context, players);
    readAppearanceCells(row, context);
    readMetrics(row, context.source, mapping, metrics);
    return row;
}

std::vector<MatchGroup> groupMatches(std::vector<MatchRow>& rows) {
    std::vector<MatchGroup> groups;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        MatchRow& row = rows[index];
        if (row.templateExample || !row.date || !row.opponentKey) {
            continue;
        }
        auto group = std::ranges::find_if(groups, [&](const MatchGroup& candidate) {
            return candidate.date == *row.date && candidate.opponentKey == *row.opponentKey;
        });
        if (group == groups.end()) {
            MatchGroup created;
            created.date = *row.date;
            created.opponent = row.opponent;
            created.opponentKey = *row.opponentKey;
            groups.push_back(std::move(created));
            group = std::prev(groups.end());
        }
        group->rows.push_back(index);
        row.match = static_cast<std::size_t>(std::distance(groups.begin(), group));
    }
    for (MatchGroup& group : groups) {
        const std::span<const std::size_t> members(group.rows);
        markInconsistent(rows, members, &MatchRow::venue, "venue", group.venue);
        std::optional<std::string> competitionKey;
        markInconsistent(rows, members, &MatchRow::competitionKey, "competition", competitionKey);
        if (competitionKey) {
            for (const std::size_t index : group.rows) {
                if (rows[index].competition) {
                    group.competition = rows[index].competition;
                    break;
                }
            }
        }
        markInconsistent(rows, members, &MatchRow::score, "score", group.score);
        markDuplicatePlayers(rows, members, [](const MatchRow&) { return false; });
        bool venueGiven = false;
        for (const std::size_t index : group.rows) {
            const MatchRow& row = rows[index];
            venueGiven |= row.venue.has_value();
            if (row.minutes && row.status() != RowStatus::Error) {
                group.maxMinutes = std::max(group.maxMinutes, *row.minutes);
            }
        }
        if (!venueGiven) {
            const auto first = std::ranges::find_if(group.rows, [&](std::size_t index) {
                return rows[index].status() != RowStatus::Error;
            });
            if (first != group.rows.end()) {
                addIssue(rows[*first], ActivityIssueKind::VenueDefault, IssueSeverity::Warning,
                         "venue");
            }
        }
    }
    return groups;
}

bool sameAppearance(const AppearanceDraft& left, const AppearanceDraft& right) {
    return left.started == right.started && left.minuteOn == right.minuteOn &&
           left.minuteOff == right.minuteOff && left.rating == right.rating &&
           left.rpe == right.rpe;
}

WriteKind changeKind(bool same, const ActivityOptions& options) {
    if (same) {
        return WriteKind::Unchanged;
    }
    return options.updateExisting ? WriteKind::Update : WriteKind::Keep;
}

std::string scoreText(std::optional<int> goalsFor, std::optional<int> goalsAgainst) {
    if (!goalsFor || !goalsAgainst) {
        return {};
    }
    return std::to_string(*goalsFor) + ':' + std::to_string(*goalsAgainst);
}

// Resolves one match group against the database; an issue applies to all its rows.
struct GroupResolution {
    PlannedMatch planned;
    const ExistingMatch* stored = nullptr;
    std::optional<ActivityIssue> error;
    std::optional<ActivityIssue> warning; // for the first row
};

GroupResolution resolveGroup(const MatchGroup& group, std::span<const Opponent> opponents,
                             std::span<const ExistingMatch> existing,
                             std::vector<std::string>& newOpponents) {
    GroupResolution result;
    PlannedMatch& planned = result.planned;
    planned.draft.date = group.date; // also for the report of a match with an error
    const auto opponent = std::ranges::find_if(opponents, [&](const Opponent& candidate) {
        return foldName(candidate.name) == group.opponentKey;
    });
    if (opponent != opponents.end()) {
        planned.opponent = opponent->id;
        std::vector<const ExistingMatch*> candidates;
        for (const ExistingMatch& match : existing) {
            if (match.match.date == group.date && match.match.opponentId == opponent->id) {
                candidates.push_back(&match);
            }
        }
        if (candidates.size() > 1) {
            result.error = makeIssue(ActivityIssueKind::SeveralMatches, IssueSeverity::Error,
                                     "opponent", group.opponent);
            result.error->count = static_cast<int>(candidates.size());
            return result;
        }
        if (candidates.size() == 1) {
            const ExistingMatch& match = *candidates.front();
            if (match.recorded || match.match.status == MatchStatus::Live) {
                result.error = makeIssue(ActivityIssueKind::MatchRecorded, IssueSeverity::Error,
                                         "opponent", group.opponent);
                return result;
            }
            if (match.match.status == MatchStatus::Planned) {
                result.error = makeIssue(ActivityIssueKind::MatchPlanned, IssueSeverity::Error,
                                         "opponent", group.opponent);
                return result;
            }
            result.stored = &match;
            planned.existing = match.match.id;
            planned.draft = static_cast<const MatchDraft&>(match.match);
            planned.goalsFor = match.match.goalsFor;
            planned.goalsAgainst = match.match.goalsAgainst;
            if (group.score && (match.match.goalsFor != group.score->first ||
                                match.match.goalsAgainst != group.score->second)) {
                result.warning =
                    makeIssue(ActivityIssueKind::ScoreDiffers, IssueSeverity::Warning, "score",
                              scoreText(match.match.goalsFor, match.match.goalsAgainst));
            }
            int finalMinute = group.maxMinutes;
            for (const Appearance& appearance : match.appearances) {
                finalMinute = std::max(finalMinute, appearance.minuteOff.value_or(0));
            }
            planned.finalMinute = finalMinute > 0 ? finalMinute : kDefaultFinalMinute;
            return result;
        }
        planned.draft.opponentId = opponent->id;
    } else {
        const auto known = std::ranges::find_if(newOpponents, [&](const std::string& name) {
            return foldName(name) == group.opponentKey;
        });
        if (known == newOpponents.end()) {
            newOpponents.push_back(group.opponent);
            planned.newOpponent = newOpponents.size() - 1;
        } else {
            planned.newOpponent =
                static_cast<std::size_t>(std::distance(newOpponents.begin(), known));
        }
    }
    planned.draft.date = group.date;
    planned.draft.venue = group.venue.value_or(Venue::Home);
    planned.draft.competition = group.competition;
    if (group.score) {
        planned.goalsFor = group.score->first;
        planned.goalsAgainst = group.score->second;
    }
    planned.finalMinute = group.maxMinutes > 0 ? group.maxMinutes : kDefaultFinalMinute;
    return result;
}

} // namespace

std::string_view toKey(MatchField field) noexcept { return kFieldKeys[indexOf(field)]; }

std::optional<MatchField> matchFieldFromKey(std::string_view key) noexcept {
    for (const MatchField field : kMatchFields) {
        if (toKey(field) == key) {
            return field;
        }
    }
    return std::nullopt;
}

ActivityMapping autoMapMatches(std::span<const std::string> headers,
                               std::span<const MetricDefinition> metrics) {
    return autoMapActivity(headers, kSynonyms, metrics);
}

std::vector<ActivityMappingIssue> checkMatchMapping(const ActivityMapping& mapping) {
    std::vector<ActivityMappingIssue> issues;
    const auto columns = columnsOf(mapping, kFieldCount);
    const auto has = [&](MatchField field) { return columns[indexOf(field)].has_value(); };
    if (!has(MatchField::Date)) {
        issues.push_back(ActivityMappingIssue::MissingDate);
    }
    if (!has(MatchField::Opponent)) {
        issues.push_back(ActivityMappingIssue::MissingOpponent);
    }
    if (!has(MatchField::Player) && !has(MatchField::ShirtNumber)) {
        issues.push_back(ActivityMappingIssue::MissingPlayer);
    }
    const bool metric = std::ranges::any_of(
        mapping.columns, [](const ColumnTarget& target) { return target.metric.has_value(); });
    if (!has(MatchField::Started) && !has(MatchField::Minutes) && !has(MatchField::Rating) &&
        !has(MatchField::Rpe) && !metric) {
        issues.push_back(ActivityMappingIssue::MissingValues);
    }
    std::ranges::copy(duplicateIssues(mapping), std::back_inserter(issues));
    return issues;
}

std::optional<Venue> parseVenue(std::string_view text) {
    const std::string folded = foldText(text);
    for (const VenueSynonyms& synonyms : kVenues) {
        if (std::ranges::find(synonyms.names, folded) != synonyms.names.end()) {
            return synonyms.venue;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> parseScore(std::string_view text) {
    std::string value = trimmed(text);
    // The en dash (U+2013) as Excel autocorrects "2-1".
    for (std::size_t at = value.find("\xE2\x80\x93"); at != std::string::npos;
         at = value.find("\xE2\x80\x93")) {
        value.replace(at, 3, "-");
    }
    const std::size_t separator = value.find_first_of(":-");
    if (separator == std::string::npos) {
        return std::nullopt;
    }
    const WholeNumberCell left = readWholeNumber(value.substr(0, separator), 0, kMaxGoals);
    const WholeNumberCell right = readWholeNumber(value.substr(separator + 1), 0, kMaxGoals);
    if (!left.value || !right.value) {
        return std::nullopt;
    }
    return std::pair{*left.value, *right.value};
}

MatchImport validateMatches(const SourceTable& table, const ActivityMapping& mapping,
                            std::span<const Player> players,
                            std::span<const MetricDefinition> metrics, bool excelSerial,
                            const std::function<void(std::size_t)>& progress) {
    MatchImport result;
    const auto columns = columnsOf(mapping, kFieldCount);
    result.rows.reserve(table.rows.size());
    for (const SourceRow& source : table.rows) {
        result.rows.push_back(
            readRow(RowContext{source, columns}, mapping, players, metrics, excelSerial));
        if (progress) {
            progress(result.rows.size());
        }
    }
    result.matches = groupMatches(result.rows);
    return result;
}

AppearanceDraft appearanceFor(bool started, std::optional<int> minutes, int finalMinute) {
    AppearanceDraft appearance;
    appearance.started = started;
    if (started) {
        appearance.minuteOn = 0;
        appearance.minuteOff = minutes.value_or(finalMinute);
    } else if (minutes && *minutes > 0) {
        appearance.minuteOn = std::max(0, finalMinute - *minutes);
        appearance.minuteOff = finalMinute;
    }
    return appearance;
}

MatchPlan planMatches(const MatchImport& import, std::span<const Opponent> opponents,
                      std::span<const ExistingMatch> existing, std::span<const Player> players,
                      std::span<const MetricDefinition> metrics, const ActivityOptions& options) {
    MatchPlan plan;
    plan.metrics = metricsOfRows(import.rows, metrics);
    std::vector<std::vector<std::pair<PlayerId, int>>> playersOf(import.matches.size());
    std::vector<GroupResolution> groups;
    groups.reserve(import.matches.size());
    for (const MatchGroup& group : import.matches) {
        groups.push_back(resolveGroup(group, opponents, existing, plan.newOpponents));
        plan.matches.push_back(groups.back().planned);
    }

    plan.rows.resize(import.rows.size());
    std::vector<bool> warned(import.matches.size(), false);
    for (std::size_t index = 0; index < import.rows.size(); ++index) {
        const MatchRow& row = import.rows[index];
        PlannedMatchRow& out = plan.rows[index];
        out.match = row.match;
        if (row.status() == RowStatus::Error) {
            out.action = ActivityAction::Invalid;
            continue;
        }
        if (row.templateExample || !row.match) {
            out.action = ActivityAction::Skip;
            continue;
        }
        const GroupResolution& group = groups[*row.match];
        if (group.error) {
            out.issues.push_back(*group.error);
            out.action = ActivityAction::Invalid;
            continue;
        }
        if (group.warning && !warned[*row.match]) {
            out.issues.push_back(*group.warning);
            warned[*row.match] = true;
        }
        // The squad and the metrics of now, not those of the validation (review of P18).
        std::vector<ActivityIssue> checks;
        const RowPlayer player = currentPlayer(row.player, players, checks);
        const std::vector<MetricCell> cells = currentMetrics(row.metrics, metrics, checks);
        out.issues.insert(out.issues.end(), checks.begin(), checks.end());
        if (hasErrors(checks)) {
            out.action = ActivityAction::Invalid;
            continue;
        }
        PlayerResolution resolution =
            resolvePlayer(player, row.sourceRow, options, players, plan.newPlayers);
        if (resolution.issue) {
            out.issues.push_back(*resolution.issue);
        }
        if (!resolution.player) {
            out.action = hasErrors(out.issues) ? ActivityAction::Invalid : ActivityAction::Skip;
            continue;
        }
        out.player = *resolution.player;
        // Two rows that are one player now (numbers swapped after the check).
        if (out.player.existing) {
            auto& taken = playersOf[*row.match];
            const auto same =
                std::ranges::find(taken, *out.player.existing, &std::pair<PlayerId, int>::first);
            if (same != taken.end()) {
                out.issues.push_back(makeIssue(ActivityIssueKind::DuplicateInFile,
                                               IssueSeverity::Error, "player", player.text,
                                               {same->second}));
                out.action = ActivityAction::Invalid;
                continue;
            }
            taken.emplace_back(*out.player.existing, row.sourceRow);
        }
        PlannedMatch& target = plan.matches[*row.match];
        const ExistingMatch* stored = out.player.existing ? group.stored : nullptr;
        std::vector<WriteKind> writes;

        AppearanceDraft appearance =
            appearanceFor(row.started.value_or(false), row.minutes, target.finalMinute);
        appearance.playerId = out.player.existing.value_or(PlayerId{});
        appearance.rating = row.rating;
        appearance.rpe = row.rpe;
        out.appearanceWrite = WriteKind::Create;
        if (stored != nullptr) {
            const auto previous =
                std::ranges::find(stored->appearances, appearance.playerId, &Appearance::playerId);
            if (previous != stored->appearances.end()) {
                out.previousAppearance = static_cast<const AppearanceDraft&>(*previous);
                appearance.position = previous->position;
                out.appearanceWrite =
                    changeKind(sameAppearance(*out.previousAppearance, appearance), options);
            }
        }
        out.appearance = appearance;
        writes.push_back(out.appearanceWrite);
        for (const MetricCell& cell : cells) {
            PlannedValue value{cell.metric, cell.value, WriteKind::Create, std::nullopt};
            if (stored != nullptr) {
                const auto previous =
                    std::ranges::find_if(stored->values, [&](const MetricValue& candidate) {
                        return candidate.metricId == cell.metric &&
                               candidate.playerId == *out.player.existing;
                    });
                if (previous != stored->values.end()) {
                    value.previous = previous->value;
                    value.write = changeKind(previous->value == cell.value, options);
                }
            }
            writes.push_back(value.write);
            out.values.push_back(value);
        }
        out.action = combineWrites(writes);
        if (std::ranges::find(writes, WriteKind::Keep) != writes.end()) {
            out.issues.push_back(
                makeIssue(ActivityIssueKind::ExistingKept, IssueSeverity::Warning, {}));
        }
        if (out.action == ActivityAction::Create || out.action == ActivityAction::Update) {
            target.written = true;
        }
    }
    return plan;
}

NotImportedEvents notImportedEvents(const MatchImport& import, const MatchPlan& plan) {
    NotImportedEvents totals;
    for (std::size_t index = 0; index < import.rows.size() && index < plan.rows.size(); ++index) {
        const ActivityAction action = plan.rows[index].action;
        if (action != ActivityAction::Create && action != ActivityAction::Update &&
            action != ActivityAction::Unchanged && action != ActivityAction::KeepExisting) {
            continue;
        }
        const MatchRow& row = import.rows[index];
        totals.goals += row.goals;
        totals.assists += row.assists;
        totals.cards += row.cards;
        totals.yellowCards += row.yellowCards;
    }
    return totals;
}

} // namespace fdss::core
