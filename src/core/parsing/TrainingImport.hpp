#pragma once

#include "core/domain/Attendance.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Metric.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/parsing/ActivityImport.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Training import (packet P18 requirement 1): one row per player per session. Validation reads the
// file alone; planTraining() compares it with the sessions already in the database.

namespace fdss::core {

enum class TrainingField {
    Date,
    StartTime,
    Duration,
    Type,
    Player,
    ShirtNumber,
    Status,
    Rpe,
    Minutes
};
inline constexpr std::array kTrainingFields = {
    TrainingField::Date,   TrainingField::StartTime, TrainingField::Duration,
    TrainingField::Type,   TrainingField::Player,    TrainingField::ShirtNumber,
    TrainingField::Status, TrainingField::Rpe,       TrainingField::Minutes};

// "date", "start_time", "duration", "type", "player", "shirt_number", "status", "rpe", "minutes".
[[nodiscard]] std::string_view toKey(TrainingField field) noexcept;
[[nodiscard]] std::optional<TrainingField> trainingFieldFromKey(std::string_view key) noexcept;

// Fields by PL/EN synonyms; other columns by the names of the training metrics.
[[nodiscard]] ActivityMapping autoMapTraining(std::span<const std::string> headers,
                                              std::span<const MetricDefinition> metrics);
[[nodiscard]] std::vector<ActivityMappingIssue>
checkTrainingMapping(const ActivityMapping& mapping);

// obecny/present, nieobecny/absent, usprawiedliwiony/excused, kontuzja/chory/injured… nullopt for
// anything else.
[[nodiscard]] std::optional<AttendanceStatus> parseAttendanceStatus(std::string_view text);
// trening/training, przedmeczowy/match prep, regeneracja/recovery, siłownia/gym, inne/other.
[[nodiscard]] std::optional<SessionType> parseSessionType(std::string_view text);

struct TrainingRow {
    int sourceRow = 0;
    std::optional<Date> date;
    std::optional<TimeOfDay> startTime;
    std::optional<int> duration;
    std::optional<SessionType> type;
    RowPlayer player;
    // Empty: the row writes no attendance (only metric values, or nothing).
    std::optional<AttendanceStatus> attendance;
    std::optional<int> rpe;     // Present only
    std::optional<int> minutes; // Present only
    std::vector<MetricCell> metrics;
    bool templateExample = false;
    bool nothingToImport = false;
    std::optional<std::size_t> session; // the session group, for a row with a date
    std::vector<ActivityIssue> issues;

    [[nodiscard]] RowStatus status() const noexcept { return statusOf(issues); }
    friend bool operator==(const TrainingRow&, const TrainingRow&) = default;
};

// The rows of one session of the file: the same date and start time.
struct TrainingSessionGroup {
    Date date{};
    std::optional<TimeOfDay> startTime;
    std::optional<int> duration;     // from the file, the same in every row that has it
    std::optional<SessionType> type; // likewise
    int maxMinutes = 0;              // the largest minutes of its rows without errors
    std::vector<std::size_t> rows;

    friend bool operator==(const TrainingSessionGroup&, const TrainingSessionGroup&) = default;
};

struct TrainingImport {
    std::vector<TrainingRow> rows;
    std::vector<TrainingSessionGroup> sessions;

    friend bool operator==(const TrainingImport&, const TrainingImport&) = default;
};

// Every row of the table, in order. `players`: the players of the team who are not archived;
// `metrics`: the definitions the mapping may name. `excelSerial`: the table comes from XLSX.
// `progress` gets the number of rows done.
[[nodiscard]] TrainingImport
validateTraining(const SourceTable& table, const ActivityMapping& mapping,
                 std::span<const Player> players, std::span<const MetricDefinition> metrics,
                 bool excelSerial, const std::function<void(std::size_t)>& progress = {});

// The duration of a new session of the group (packet P18 decision 6): from the file, otherwise
// the largest minutes of its rows (15-240), otherwise 90.
[[nodiscard]] int sessionDuration(const TrainingSessionGroup& group) noexcept;

// What the database holds for a session of the file's dates.
struct ExistingSession {
    TrainingSession session;
    std::vector<Attendance> attendance;
    std::vector<MetricValue> values; // of the metrics of the mapping

    friend bool operator==(const ExistingSession&, const ExistingSession&) = default;
};

struct PlannedSession {
    std::optional<TrainingSessionId> existing;
    TrainingSessionDraft draft; // a new session; the stored one for an existing session
    bool written = false;       // at least one row writes into it

    friend bool operator==(const PlannedSession&, const PlannedSession&) = default;
};

struct PlannedTrainingRow {
    ActivityAction action = ActivityAction::Skip;
    std::optional<std::size_t> session; // index into TrainingPlan::sessions
    PlannedPlayer player;
    std::optional<AttendanceDraft> attendance; // playerId empty for a new player
    WriteKind attendanceWrite = WriteKind::None;
    std::optional<AttendanceDraft> previousAttendance; // Update, Keep
    std::vector<PlannedValue> values;
    std::vector<ActivityIssue> issues; // of the plan; the row keeps those of the validation

    friend bool operator==(const PlannedTrainingRow&, const PlannedTrainingRow&) = default;
};

struct TrainingPlan {
    std::vector<PlannedSession> sessions;
    std::vector<NewPlayer> newPlayers;
    std::vector<PlannedTrainingRow> rows; // one per row of the import

    friend bool operator==(const TrainingPlan&, const TrainingPlan&) = default;
};

// Sessions: the same date and start time; without a start time the only session of that day
// (several: SeveralSessions). Attendance and metric values per decision 3: Create, Unchanged, or
// Update/Keep by options.updateExisting. `existing`: the sessions of the team on the dates of the
// file; `players`: the players of the team who are not archived.
[[nodiscard]] TrainingPlan planTraining(const TrainingImport& import,
                                        std::span<const ExistingSession> existing,
                                        std::span<const Player> players,
                                        const ActivityOptions& options);

} // namespace fdss::core
