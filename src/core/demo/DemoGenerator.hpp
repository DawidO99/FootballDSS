#pragma once

#include "core/domain/Attendance.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/TrainingSession.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The demo club (SPEC section 9, packet P10): half a season of a fictional B-klasa team. Pure and
// deterministic: the same seed and start date give the same data on every platform, so the
// generator has its own random numbers and distributions (the standard ones differ between
// standard libraries) and uses integer arithmetic only.
//
// Rows refer to each other by index into the vectors of DemoDataset, never by database id.

namespace fdss::core::demo {

inline constexpr std::uint64_t kDemoSeed = 0x4644'5353'2D50'3130ULL; // "FDSS-P10"
inline constexpr std::string_view kDemoStaffPin = "1234";
// The data starts this many days before today.
inline constexpr int kDemoDays = 84;
inline constexpr int kActivePlayers = 22;
inline constexpr int kArchivedPlayers = 2;
inline constexpr int kOpponents = 8;
inline constexpr int kMatchMinutes = 90;
inline constexpr int kMaxSubstitutions = 5;
inline constexpr std::string_view kDemoCompetition = "B-klasa";

// SplitMix64 (Steele, Lea, Flood 2014): fixed output for a fixed seed on every platform.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) noexcept : m_state(seed) {}

    std::uint64_t next() noexcept;
    // Uniform in [lo, hi], both included, without modulo bias. lo <= hi.
    int uniform(int lo, int hi) noexcept;
    // True with the given chance in percent.
    bool chance(int percent) noexcept;
    // The rounded mean of `rolls` uniform draws in [lo, hi]: a bell shape in integers only.
    int bell(int lo, int hi, int rolls = 3) noexcept;

private:
    std::uint64_t m_state;
};

struct DemoStaff {
    std::string_view name;
    Role role = Role::Assistant;
};

// The Assistant and the Physio the demo adds next to the Coach typed in the wizard. Fixed, so the
// wizard can name them before anything is generated.
inline constexpr std::array<DemoStaff, 2> kDemoStaff{
    DemoStaff{"Tomasz Wrona", Role::Assistant},
    DemoStaff{"Anna Sowa", Role::Physio},
};

struct DemoPlayer {
    PlayerDraft draft;
    std::optional<Date> archivedAt;
};

struct DemoAvailability {
    int player = 0;
    AvailabilityDraft draft;
};

struct DemoAttendance {
    int player = 0;
    AttendanceStatus status = AttendanceStatus::Present;
    std::optional<int> rpe;
};

struct DemoSession {
    TrainingSessionDraft draft;
    // Empty for today and the planned sessions after it.
    std::vector<DemoAttendance> attendance;
};

struct DemoAppearance {
    int player = 0;
    bool started = false;
    int minuteOn = 0;
    int minuteOff = 0;
    Position position = Position::Cm;
    std::optional<int> rating;
    std::optional<int> rpe;
};

struct DemoEvent {
    int minute = 0;
    MatchEventType type = MatchEventType::Goal;
    std::optional<int> player;        // empty only for OpponentGoal
    std::optional<int> relatedPlayer; // sub: the player going off; goal: the assisting player
};

struct DemoMatch {
    int opponent = 0;
    // draft.opponentId is not used: `opponent` is the index.
    MatchDraft draft;
    MatchStatus status = MatchStatus::Planned;
    std::optional<int> goalsFor;
    std::optional<int> goalsAgainst;
    std::vector<DemoAppearance> appearances; // empty for a planned match
    std::vector<DemoEvent> events;           // by minute
};

// Who carries the stories the later screens show; for tests and nothing else.
struct DemoStory {
    int loadSpike = 0;     // high load in the last 7 days
    int returned = 0;      // back after a closed injury
    int longInjury = 0;    // injured today, return in a few weeks
    int returningSoon = 0; // injured today, expected back within 7 days
    int suspended = 0;     // red card in the last match, misses the next one
};

struct DemoDataset {
    Date today{};
    // The active players first, then the archived ones.
    std::vector<DemoPlayer> players;
    std::vector<OpponentDraft> opponents;
    // By date.
    std::vector<DemoMatch> matches;
    std::vector<DemoSession> sessions;
    std::vector<DemoAvailability> availability;
    DemoStory story;
};

// startDate is kDemoDays before today. Same seed and startDate: the same dataset everywhere.
[[nodiscard]] DemoDataset generate(std::uint64_t seed, Date startDate);

} // namespace fdss::core::demo
