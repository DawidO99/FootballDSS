#include "core/parsing/ActivityImport.hpp"

#include "core/domain/Validation.hpp"

#include <algorithm>
#include <string>

namespace fdss::core {

std::optional<MetricDefinitionId> metricForHeader(std::string_view header,
                                                  std::span<const MetricDefinition> metrics) {
    const std::string key = metricNameKey(header);
    if (key.empty()) {
        return std::nullopt;
    }
    for (const MetricDefinition& metric : metrics) {
        if (key == metricNameKey(metric.namePl) || key == metricNameKey(metric.nameEn) ||
            key == metricNameKey(metric.key)) {
            return metric.id;
        }
    }
    return std::nullopt;
}

ActivityMapping autoMapActivity(std::span<const std::string> headers,
                                std::span<const FieldSynonyms> fields,
                                std::span<const MetricDefinition> metrics) {
    ActivityMapping mapping;
    const std::vector<std::optional<int>> matched = matchHeaders(headers, fields);
    std::vector<MetricDefinitionId> used;
    for (std::size_t column = 0; column < headers.size(); ++column) {
        ColumnTarget target{matched[column], std::nullopt};
        if (!target.field) {
            if (const auto metric = metricForHeader(headers[column], metrics);
                metric && std::ranges::find(used, *metric) == used.end()) {
                target.metric = metric;
                used.push_back(*metric);
            }
        }
        mapping.columns.push_back(target);
    }
    return mapping;
}

std::vector<std::optional<std::size_t>> columnsOf(const ActivityMapping& mapping,
                                                  std::size_t fieldCount) {
    std::vector<std::optional<std::size_t>> columns(fieldCount);
    for (std::size_t column = 0; column < mapping.columns.size(); ++column) {
        const auto& field = mapping.columns[column].field;
        if (field && *field >= 0 && static_cast<std::size_t>(*field) < fieldCount &&
            !columns[static_cast<std::size_t>(*field)]) {
            columns[static_cast<std::size_t>(*field)] = column;
        }
    }
    return columns;
}

std::vector<ActivityMappingIssue> duplicateIssues(const ActivityMapping& mapping) {
    std::vector<ActivityMappingIssue> issues;
    std::vector<int> fields;
    std::vector<MetricDefinitionId> metrics;
    bool duplicateField = false;
    bool duplicateMetric = false;
    for (const ColumnTarget& target : mapping.columns) {
        if (target.field) {
            duplicateField |= std::ranges::find(fields, *target.field) != fields.end();
            fields.push_back(*target.field);
        }
        if (target.metric) {
            duplicateMetric |= std::ranges::find(metrics, *target.metric) != metrics.end();
            metrics.push_back(*target.metric);
        }
    }
    if (duplicateField) {
        issues.push_back(ActivityMappingIssue::DuplicateField);
    }
    if (duplicateMetric) {
        issues.push_back(ActivityMappingIssue::DuplicateMetric);
    }
    return issues;
}

RowStatus statusOf(std::span<const ActivityIssue> issues) noexcept {
    RowStatus status = RowStatus::Ok;
    for (const ActivityIssue& issue : issues) {
        if (issue.severity == IssueSeverity::Error) {
            return RowStatus::Error;
        }
        status = RowStatus::Warning;
    }
    return status;
}

bool hasErrorOn(std::span<const ActivityIssue> issues, std::string_view field) {
    return std::ranges::any_of(issues, [&](const ActivityIssue& issue) {
        return issue.field == field && issue.severity == IssueSeverity::Error;
    });
}

bool hasErrors(std::span<const ActivityIssue> issues) noexcept {
    return statusOf(issues) == RowStatus::Error;
}

std::optional<PlayerDraft> newPlayerDraft(const RowPlayer& player, const ActivityOptions& options,
                                          std::span<const Player> existing) {
    if (player.text.empty() || parseWholeNumber(player.text)) {
        return std::nullopt;
    }
    auto name = splitFullName(player.text, options.nameOrder);
    if (!name || characterCount(name->firstName) > kMaxPersonNameLength ||
        characterCount(name->lastName) > kMaxPersonNameLength) {
        return std::nullopt;
    }
    PlayerDraft draft;
    draft.firstName = std::move(name->firstName);
    draft.lastName = std::move(name->lastName);
    draft.primaryPosition = options.defaultPosition;
    const bool numberTaken =
        player.shirtNumber && std::ranges::any_of(existing, [&](const Player& other) {
            return other.shirtNumber == player.shirtNumber;
        });
    if (player.shirtNumber && !numberTaken && *player.shirtNumber >= kMinShirtNumber &&
        *player.shirtNumber <= kMaxShirtNumber) {
        draft.shirtNumber = player.shirtNumber;
    }
    return draft;
}

PlayerResolution resolvePlayer(const RowPlayer& player, int sourceRow,
                               const ActivityOptions& options, std::span<const Player> existing,
                               std::vector<NewPlayer>& newPlayers) {
    PlayerResolution resolution;
    switch (player.lookup.kind) {
    case PlayerLookupKind::Found:
        resolution.player = PlannedPlayer{player.lookup.id, std::nullopt};
        return resolution;
    case PlayerLookupKind::Ambiguous:
        resolution.issue = makeIssue(ActivityIssueKind::AmbiguousPlayer, IssueSeverity::Warning,
                                     "player", player.text, {}, player.lookup.candidates);
        return resolution;
    case PlayerLookupKind::Unknown:
        break;
    }
    if (options.unknownPlayers == UnknownPlayerPolicy::Skip) {
        resolution.issue = makeIssue(ActivityIssueKind::UnknownPlayer, IssueSeverity::Warning,
                                     "player", player.text);
        return resolution;
    }
    auto draft = newPlayerDraft(player, options, existing);
    if (!draft) {
        resolution.issue = makeIssue(ActivityIssueKind::CannotCreatePlayer, IssueSeverity::Error,
                                     "player", player.text);
        return resolution;
    }
    const std::string key = personKey(draft->firstName, draft->lastName);
    auto same = std::ranges::find(newPlayers, key, &NewPlayer::key);
    if (same == newPlayers.end()) {
        if (draft->shirtNumber && std::ranges::any_of(newPlayers, [&](const NewPlayer& other) {
                return other.draft.shirtNumber == draft->shirtNumber;
            })) {
            draft->shirtNumber.reset();
        }
        newPlayers.push_back(NewPlayer{key, std::move(*draft), {}});
        same = std::prev(newPlayers.end());
    }
    same->rows.push_back(sourceRow);
    resolution.player = PlannedPlayer{
        std::nullopt, static_cast<std::size_t>(std::distance(newPlayers.begin(), same))};
    resolution.issue = makeIssue(ActivityIssueKind::NewPlayer, IssueSeverity::Warning, "player",
                                 same->draft.firstName + ' ' + same->draft.lastName);
    return resolution;
}

RowPlayer currentPlayer(const RowPlayer& validated, std::span<const Player> players,
                        std::vector<ActivityIssue>& issues) {
    RowPlayer current = validated;
    current.lookup = matchPlayer(validated.text, validated.shirtNumber, players);
    if (current.lookup != validated.lookup) {
        issues.push_back(makeIssue(ActivityIssueKind::PlanChanged, IssueSeverity::Warning, "player",
                                   validated.text));
    }
    return current;
}

std::vector<MetricCell> currentMetrics(std::span<const MetricCell> cells,
                                       std::span<const MetricDefinition> metrics,
                                       std::vector<ActivityIssue>& issues) {
    std::vector<MetricCell> current;
    for (const MetricCell& cell : cells) {
        const auto metric = std::ranges::find(metrics, cell.metric, &MetricDefinition::id);
        std::optional<double> value;
        if (metric != metrics.end()) {
            value = parseMetricValue(cell.text, metric->valueType);
        }
        if (!value || !validateValue(*metric, *value).empty()) {
            issues.push_back(makeIssue(
                ActivityIssueKind::MetricChanged, IssueSeverity::Error,
                metric != metrics.end() ? "metric:" + metric->key : std::string{}, cell.text));
            continue;
        }
        current.push_back(MetricCell{cell.metric, cell.text, *value});
    }
    return current;
}

ActivityAction combineWrites(std::span<const WriteKind> writes) noexcept {
    const auto has = [&](WriteKind kind) {
        return std::ranges::find(writes, kind) != writes.end();
    };
    if (has(WriteKind::Update)) {
        return ActivityAction::Update;
    }
    if (has(WriteKind::Create)) {
        return ActivityAction::Create;
    }
    if (has(WriteKind::Keep)) {
        return ActivityAction::KeepExisting;
    }
    if (has(WriteKind::Unchanged)) {
        return ActivityAction::Unchanged;
    }
    return ActivityAction::Skip;
}

WholeNumberCell readWholeNumber(std::string_view text, int min, int max) {
    const std::string value = trimmed(text);
    if (value.empty()) {
        return {};
    }
    const auto number = parseWholeNumber(value);
    if (!number) {
        const bool decimal = parseMetricValue(value, MetricValueType::Decimal).has_value();
        return {std::nullopt,
                decimal ? ActivityIssueKind::NotAWholeNumber : ActivityIssueKind::NotANumber};
    }
    if (*number < min || *number > max) {
        return {std::nullopt, ActivityIssueKind::OutOfRange};
    }
    return {number, std::nullopt};
}

std::string duplicatePlayerKey(const RowPlayer& player) {
    switch (player.lookup.kind) {
    case PlayerLookupKind::Found:
        return "id:" + std::to_string(player.lookup.id->value);
    case PlayerLookupKind::Unknown:
        if (!player.text.empty()) {
            return "name:" + foldName(player.text);
        }
        return player.shirtNumber ? "nr:" + std::to_string(*player.shirtNumber) : std::string{};
    case PlayerLookupKind::Ambiguous:
        break;
    }
    return {};
}

bool isTemplateExamplePlayer(std::string_view playerCell) {
    return foldName(playerCell) == "jan przykladowy";
}

} // namespace fdss::core
