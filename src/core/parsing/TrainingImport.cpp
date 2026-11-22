#include "core/parsing/TrainingImport.hpp"

#include "core/domain/Validation.hpp"
#include "core/parsing/HeaderMatching.hpp"
#include "core/parsing/ImportValues.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace fdss::core {

namespace {

constexpr std::size_t kFieldCount = kTrainingFields.size();

constexpr std::array<std::string_view, kFieldCount> kFieldKeys = {
    "date", "start_time", "duration", "type", "player", "shirt_number", "status", "rpe", "minutes"};

// Folded (foldText).
constexpr std::array<std::string_view, 9> kDate = {
    "data", "dzien",        "data treningu", "data zajec",    "date",
    "day",  "session date", "training date", "dzien treningu"};
constexpr std::array<std::string_view, 9> kStartTime = {
    "godzina", "godz",       "start",     "poczatek",        "godzina rozpoczecia",
    "time",    "start time", "starts at", "godzina treningu"};
constexpr std::array<std::string_view, 10> kDuration = {
    "czas trwania", "dlugosc", "czas zajec",   "czas treningu",  "dlugosc treningu",
    "duration",     "length",  "duration min", "session length", "czas trwania min"};
constexpr std::array<std::string_view, 8> kType = {
    "typ", "rodzaj", "typ zajec", "rodzaj zajec", "typ treningu", "type", "session type", "kind"};
constexpr std::array<std::string_view, 15> kPlayer = {
    "zawodnik",  "pilkarz",       "gracz",         "imie i nazwisko", "nazwisko i imie",
    "nazwisko",  "imie nazwisko", "nazwisko imie", "player",          "name",
    "full name", "player name",   "surname",       "last name",       "zawodniczka"};
constexpr std::array<std::string_view, 9> kShirtNumber = {
    "nr", "numer", "nr koszulki", "numer koszulki", "number", "no", "shirt", "shirt number", "#"};
constexpr std::array<std::string_view, 7> kStatus = {
    "obecnosc", "status", "frekwencja", "obecny", "attendance", "presence", "present"};
constexpr std::array<std::string_view, 7> kRpe = {
    "rpe", "rpe 0 10", "wysilek", "odczuwany wysilek", "borg", "perceived exertion", "rpe cr10"};
constexpr std::array<std::string_view, 8> kMinutes = {
    "minuty",  "min",  "minuty treningu", "czas udzialu",
    "minutes", "mins", "minutes trained", "minuty zawodnika"};

const std::array<FieldSynonyms, kFieldCount> kSynonyms = {
    FieldSynonyms{static_cast<int>(TrainingField::Date), kDate},
    FieldSynonyms{static_cast<int>(TrainingField::StartTime), kStartTime},
    FieldSynonyms{static_cast<int>(TrainingField::Duration), kDuration},
    FieldSynonyms{static_cast<int>(TrainingField::Type), kType},
    FieldSynonyms{static_cast<int>(TrainingField::Player), kPlayer},
    FieldSynonyms{static_cast<int>(TrainingField::ShirtNumber), kShirtNumber},
    FieldSynonyms{static_cast<int>(TrainingField::Status), kStatus},
    FieldSynonyms{static_cast<int>(TrainingField::Rpe), kRpe},
    FieldSynonyms{static_cast<int>(TrainingField::Minutes), kMinutes},
};

struct ValueSynonyms {
    AttendanceStatus status;
    std::span<const std::string_view> names;
};
constexpr std::array<std::string_view, 9> kPresent = {"obecny", "obecna", "ob",      "o", "tak",
                                                      "x",      "1",      "present", "p"};
constexpr std::array<std::string_view, 8> kAbsent = {"nieobecny", "nieobecna", "nb", "nie",
                                                     "0",         "absent",    "a",  "nieob"};
constexpr std::array<std::string_view, 7> kExcused = {
    "usprawiedliwiony", "usprawiedliwiona", "uspr", "u", "excused", "e", "zwolniony"};
constexpr std::array<std::string_view, 11> kInjured = {
    "kontuzja", "kontuzjowany", "kontuzjowana", "uraz",    "k",  "injured",
    "injury",   "chory",        "chora",        "choroba", "ill"};
const std::array<ValueSynonyms, 4> kStatuses = {
    ValueSynonyms{AttendanceStatus::Present, kPresent},
    ValueSynonyms{AttendanceStatus::Absent, kAbsent},
    ValueSynonyms{AttendanceStatus::Excused, kExcused},
    ValueSynonyms{AttendanceStatus::Injured, kInjured},
};

struct TypeSynonyms {
    SessionType type;
    std::span<const std::string_view> names;
};
constexpr std::array<std::string_view, 5> kTraining = {"trening", "training", "zajecia", "tr",
                                                       "practice"};
constexpr std::array<std::string_view, 7> kMatchPrep = {
    "przedmeczowy", "trening przedmeczowy", "rozruch", "match prep", "pre match",
    "prematch",     "match preparation"};
constexpr std::array<std::string_view, 5> kRecovery = {"regeneracja", "recovery", "odnowa",
                                                       "trening regeneracyjny", "regen"};
constexpr std::array<std::string_view, 4> kGym = {"silownia", "gym", "trening silowy", "strength"};
constexpr std::array<std::string_view, 3> kOther = {"inne", "inny", "other"};
const std::array<TypeSynonyms, 5> kTypes = {
    TypeSynonyms{SessionType::Training, kTraining},
    TypeSynonyms{SessionType::MatchPrep, kMatchPrep},
    TypeSynonyms{SessionType::Recovery, kRecovery},
    TypeSynonyms{SessionType::Gym, kGym},
    TypeSynonyms{SessionType::Other, kOther},
};

std::size_t indexOf(TrainingField field) { return static_cast<std::size_t>(field); }

void addIssue(TrainingRow& row, ActivityIssueKind kind, IssueSeverity severity,
              std::string_view field, std::string value = {}) {
    row.issues.push_back(makeIssue(kind, severity, std::string(field), std::move(value)));
}

struct RowContext {
    const SourceRow& source;
    const std::vector<std::optional<std::size_t>>& columns;

    [[nodiscard]] std::string cell(TrainingField field) const {
        const auto column = columns[indexOf(field)];
        return column ? trimmed(cellAt(source, *column)) : std::string{};
    }
};

std::optional<int> readNumberCell(TrainingRow& row, const std::string& text, TrainingField field,
                                  int min, int max) {
    const WholeNumberCell cell = readWholeNumber(text, min, max);
    if (cell.issue) {
        addIssue(row, *cell.issue, IssueSeverity::Error, toKey(field), text);
    }
    return cell.value;
}

void readMetrics(TrainingRow& row, const SourceRow& source, const ActivityMapping& mapping,
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

TrainingRow readRow(const RowContext& context, const ActivityMapping& mapping,
                    std::span<const Player> players, std::span<const MetricDefinition> metrics,
                    bool excelSerial) {
    TrainingRow row;
    row.sourceRow = context.source.number;
    row.player.text = context.cell(TrainingField::Player);
    if (isTemplateExamplePlayer(row.player.text)) {
        row.templateExample = true;
        addIssue(row, ActivityIssueKind::TemplateExample, IssueSeverity::Warning, "player",
                 row.player.text);
        return row;
    }

    const std::string date = context.cell(TrainingField::Date);
    if (date.empty()) {
        addIssue(row, ActivityIssueKind::MissingValue, IssueSeverity::Error, "date");
    } else if (row.date = parseImportDate(date, excelSerial); !row.date) {
        addIssue(row, ActivityIssueKind::NotADate, IssueSeverity::Error, "date", date);
    }
    if (const std::string time = context.cell(TrainingField::StartTime); !time.empty()) {
        if (row.startTime = parseImportTime(time); !row.startTime) {
            addIssue(row, ActivityIssueKind::NotATime, IssueSeverity::Error, "start_time", time);
        }
    }
    row.duration =
        readNumberCell(row, context.cell(TrainingField::Duration), TrainingField::Duration,
                       kMinSessionDuration, kMaxSessionDuration);
    if (const std::string type = context.cell(TrainingField::Type); !type.empty()) {
        if (row.type = parseSessionType(type); !row.type) {
            addIssue(row, ActivityIssueKind::UnknownValue, IssueSeverity::Warning, "type", type);
        }
    }

    const std::string number = context.cell(TrainingField::ShirtNumber);
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

    if (const std::string status = context.cell(TrainingField::Status); !status.empty()) {
        if (row.attendance = parseAttendanceStatus(status); !row.attendance) {
            addIssue(row, ActivityIssueKind::UnknownValue, IssueSeverity::Error, "status", status);
        }
    }
    row.rpe = readNumberCell(row, context.cell(TrainingField::Rpe), TrainingField::Rpe, 0, kMaxRpe);
    row.minutes = readNumberCell(row, context.cell(TrainingField::Minutes), TrainingField::Minutes,
                                 0, kMaxAttendanceMinutes);
    readMetrics(row, context.source, mapping, metrics);

    // Packet P18 decision 6.
    if (!row.attendance && !hasErrorOn(row.issues, "status") && (row.rpe || row.minutes)) {
        row.attendance = AttendanceStatus::Present;
    }
    if (row.attendance && *row.attendance != AttendanceStatus::Present &&
        (row.rpe || row.minutes)) {
        addIssue(row, ActivityIssueKind::ValuesDropped, IssueSeverity::Warning, "status",
                 context.cell(TrainingField::Status));
        row.rpe.reset();
        row.minutes.reset();
    }
    if (!row.attendance && row.metrics.empty() && row.status() != RowStatus::Error) {
        row.nothingToImport = true;
        addIssue(row, ActivityIssueKind::NothingToImport, IssueSeverity::Warning, {});
    }
    return row;
}

std::vector<TrainingSessionGroup> groupSessions(std::vector<TrainingRow>& rows) {
    std::vector<TrainingSessionGroup> groups;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        TrainingRow& row = rows[index];
        if (row.templateExample || !row.date) {
            continue;
        }
        auto group = std::ranges::find_if(groups, [&](const TrainingSessionGroup& candidate) {
            return candidate.date == *row.date && candidate.startTime == row.startTime;
        });
        if (group == groups.end()) {
            groups.push_back(TrainingSessionGroup{*row.date, row.startTime, {}, {}, 0, {}});
            group = std::prev(groups.end());
        }
        group->rows.push_back(index);
        row.session = static_cast<std::size_t>(std::distance(groups.begin(), group));
    }
    for (TrainingSessionGroup& group : groups) {
        markInconsistent(rows, std::span<const std::size_t>(group.rows), &TrainingRow::duration,
                         "duration", group.duration);
        markInconsistent(rows, std::span<const std::size_t>(group.rows), &TrainingRow::type, "type",
                         group.type);
        markDuplicatePlayers(rows, std::span<const std::size_t>(group.rows),
                             [](const TrainingRow& row) { return row.nothingToImport; });
        for (const std::size_t index : group.rows) {
            const TrainingRow& row = rows[index];
            if (row.minutes && row.status() != RowStatus::Error) {
                group.maxMinutes = std::max(group.maxMinutes, *row.minutes);
            }
        }
    }
    return groups;
}

bool sameAttendance(const AttendanceDraft& left, const AttendanceDraft& right) {
    return left.status == right.status && left.rpe == right.rpe &&
           left.minutesOverride == right.minutesOverride;
}

AttendanceDraft draftOf(const Attendance& attendance) {
    return static_cast<const AttendanceDraft&>(attendance);
}

WriteKind changeKind(bool same, const ActivityOptions& options) {
    if (same) {
        return WriteKind::Unchanged;
    }
    return options.updateExisting ? WriteKind::Update : WriteKind::Keep;
}

} // namespace

std::string_view toKey(TrainingField field) noexcept { return kFieldKeys[indexOf(field)]; }

std::optional<TrainingField> trainingFieldFromKey(std::string_view key) noexcept {
    for (const TrainingField field : kTrainingFields) {
        if (toKey(field) == key) {
            return field;
        }
    }
    return std::nullopt;
}

ActivityMapping autoMapTraining(std::span<const std::string> headers,
                                std::span<const MetricDefinition> metrics) {
    return autoMapActivity(headers, kSynonyms, metrics);
}

std::vector<ActivityMappingIssue> checkTrainingMapping(const ActivityMapping& mapping) {
    std::vector<ActivityMappingIssue> issues;
    const auto columns = columnsOf(mapping, kFieldCount);
    const auto has = [&](TrainingField field) { return columns[indexOf(field)].has_value(); };
    if (!has(TrainingField::Date)) {
        issues.push_back(ActivityMappingIssue::MissingDate);
    }
    if (!has(TrainingField::Player) && !has(TrainingField::ShirtNumber)) {
        issues.push_back(ActivityMappingIssue::MissingPlayer);
    }
    const bool metric = std::ranges::any_of(
        mapping.columns, [](const ColumnTarget& target) { return target.metric.has_value(); });
    if (!has(TrainingField::Status) && !has(TrainingField::Rpe) && !has(TrainingField::Minutes) &&
        !metric) {
        issues.push_back(ActivityMappingIssue::MissingValues);
    }
    std::ranges::copy(duplicateIssues(mapping), std::back_inserter(issues));
    return issues;
}

std::optional<AttendanceStatus> parseAttendanceStatus(std::string_view text) {
    const std::string folded = foldText(text);
    for (const ValueSynonyms& synonyms : kStatuses) {
        if (std::ranges::find(synonyms.names, folded) != synonyms.names.end()) {
            return synonyms.status;
        }
    }
    return std::nullopt;
}

std::optional<SessionType> parseSessionType(std::string_view text) {
    const std::string folded = foldText(text);
    for (const TypeSynonyms& synonyms : kTypes) {
        if (std::ranges::find(synonyms.names, folded) != synonyms.names.end()) {
            return synonyms.type;
        }
    }
    return std::nullopt;
}

TrainingImport validateTraining(const SourceTable& table, const ActivityMapping& mapping,
                                std::span<const Player> players,
                                std::span<const MetricDefinition> metrics, bool excelSerial,
                                const std::function<void(std::size_t)>& progress) {
    TrainingImport result;
    const auto columns = columnsOf(mapping, kFieldCount);
    result.rows.reserve(table.rows.size());
    for (const SourceRow& source : table.rows) {
        result.rows.push_back(
            readRow(RowContext{source, columns}, mapping, players, metrics, excelSerial));
        if (progress) {
            progress(result.rows.size());
        }
    }
    result.sessions = groupSessions(result.rows);
    return result;
}

int sessionDuration(const TrainingSessionGroup& group) noexcept {
    constexpr int kDefaultDuration = 90;
    if (group.duration) {
        return *group.duration;
    }
    if (group.maxMinutes > 0) {
        return std::clamp(group.maxMinutes, kMinSessionDuration, kMaxSessionDuration);
    }
    return kDefaultDuration;
}

TrainingPlan planTraining(const TrainingImport& import, std::span<const ExistingSession> existing,
                          std::span<const Player> players,
                          std::span<const MetricDefinition> metrics,
                          const ActivityOptions& options) {
    TrainingPlan plan;
    plan.metrics = metricsOfRows(import.rows, metrics);
    std::vector<std::vector<std::pair<PlayerId, int>>> playersOf(import.sessions.size());
    std::vector<std::optional<ActivityIssue>> groupIssues(import.sessions.size());
    std::vector<const ExistingSession*> stored(import.sessions.size(), nullptr);
    for (std::size_t index = 0; index < import.sessions.size(); ++index) {
        const TrainingSessionGroup& group = import.sessions[index];
        std::vector<const ExistingSession*> candidates;
        for (const ExistingSession& session : existing) {
            if (session.session.date == group.date &&
                (!group.startTime || session.session.startTime == group.startTime)) {
                candidates.push_back(&session);
            }
        }
        PlannedSession planned;
        if (!group.startTime && candidates.size() > 1) {
            groupIssues[index] =
                makeIssue(ActivityIssueKind::SeveralSessions, IssueSeverity::Error, "start_time");
            groupIssues[index]->count = static_cast<int>(candidates.size());
        } else if (!candidates.empty()) {
            stored[index] = candidates.front();
            planned.existing = candidates.front()->session.id;
            planned.draft = static_cast<const TrainingSessionDraft&>(candidates.front()->session);
        }
        if (!planned.existing) {
            planned.draft.date = group.date;
            planned.draft.startTime = group.startTime;
            planned.draft.durationMin = sessionDuration(group);
            planned.draft.type = group.type.value_or(SessionType::Training);
        }
        plan.sessions.push_back(std::move(planned));
    }

    plan.rows.resize(import.rows.size());
    for (std::size_t index = 0; index < import.rows.size(); ++index) {
        const TrainingRow& row = import.rows[index];
        PlannedTrainingRow& out = plan.rows[index];
        out.session = row.session;
        if (row.status() == RowStatus::Error) {
            out.action = ActivityAction::Invalid;
            continue;
        }
        if (row.templateExample || row.nothingToImport || !row.session) {
            out.action = ActivityAction::Skip;
            continue;
        }
        if (groupIssues[*row.session]) {
            out.issues.push_back(*groupIssues[*row.session]);
            out.action = ActivityAction::Invalid;
            continue;
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
        if (resolution.positionIssue) {
            out.issues.push_back(*resolution.positionIssue);
        }
        if (!resolution.player) {
            out.action = hasErrors(out.issues) ? ActivityAction::Invalid : ActivityAction::Skip;
            continue;
        }
        out.player = *resolution.player;
        // Two rows that are one player now (numbers swapped after the check).
        if (out.player.existing) {
            auto& taken = playersOf[*row.session];
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
        const ExistingSession* session = out.player.existing ? stored[*row.session] : nullptr;
        PlannedSession& target = plan.sessions[*row.session];
        std::vector<WriteKind> writes;

        if (row.attendance) {
            AttendanceDraft draft;
            draft.playerId = out.player.existing.value_or(PlayerId{});
            draft.status = *row.attendance;
            if (draft.status == AttendanceStatus::Present) {
                draft.rpe = row.rpe;
                if (row.minutes && *row.minutes != target.draft.durationMin) {
                    draft.minutesOverride = row.minutes;
                }
            }
            out.attendanceWrite = WriteKind::Create;
            if (session != nullptr) {
                const auto previous =
                    std::ranges::find(session->attendance, draft.playerId, &Attendance::playerId);
                if (previous != session->attendance.end()) {
                    out.previousAttendance = draftOf(*previous);
                    out.attendanceWrite =
                        changeKind(sameAttendance(*out.previousAttendance, draft), options);
                }
            }
            out.attendance = draft;
            writes.push_back(out.attendanceWrite);
        }
        for (const MetricCell& cell : cells) {
            PlannedValue value{cell.metric, cell.value, WriteKind::Create, std::nullopt};
            if (session != nullptr) {
                const auto previous =
                    std::ranges::find_if(session->values, [&](const MetricValue& stored) {
                        return stored.metricId == cell.metric &&
                               stored.playerId == *out.player.existing;
                    });
                if (previous != session->values.end()) {
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

} // namespace fdss::core
