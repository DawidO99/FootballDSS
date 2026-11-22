#pragma once

#include "core/algorithms/Load.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// The lineup validator (SPEC section 7.2, packet P12): a Strategy pattern, one class per rule
// (core/algorithms/LineupRules.hpp). Pure: the service gathers the facts about the players.

namespace fdss::core {

inline constexpr int kDefaultYellowCardThreshold = 4;
inline constexpr int kMinYellowCardThreshold = 2;
inline constexpr int kMaxYellowCardThreshold = 15;
// Sessions with saved attendance that the missed-sessions rule looks at.
inline constexpr int kRecentSessions = 3;

// Info: an explanation, not a problem; it changes neither the counts of errors and warnings nor
// whether the lineup may be saved (P19 decision 6).
enum class LineupSeverity { Error, Warning, Info };

enum class LineupRule {
    // Errors
    StartingEleven,
    Goalkeeper,
    Unavailable,
    Duplicate,
    // Warnings
    HighLoad,
    OutOfPosition,
    MissedSessions,
    YellowCards,
    // Info
    PositionNotGiven
};

struct LineupIssue {
    LineupRule rule = LineupRule::StartingEleven;
    LineupSeverity severity = LineupSeverity::Error;
    std::optional<PlayerId> player; // empty for StartingEleven and Goalkeeper
    std::optional<int> slot;        // index into Lineup::pitchSlots
    std::optional<int> benchIndex;
    // StartingEleven: players in the XI; YellowCards: cards so far; MissedSessions: sessions.
    int count = 0;
    PlayerStatus status = PlayerStatus::Available; // Unavailable
    std::optional<Position> slotPosition;          // OutOfPosition, PositionNotGiven

    friend bool operator==(const LineupIssue&, const LineupIssue&) = default;
};

struct LineupPlayerFacts {
    // Empty: not given; the position check does not apply then (PositionNotGiven, P19).
    std::optional<Position> primaryPosition;
    std::vector<Position> secondaryPositions;
    PlayerStatus statusOnMatchDay = PlayerStatus::Available;
    // Empty while not known: the load is computed in the background (P11).
    std::optional<LoadStatus> loadStatus;
    // Not present in any of the last kRecentSessions sessions with saved attendance.
    bool missedRecentSessions = false;
    // In this competition and season, without matches with a red card (P12 decision 3).
    int yellowCards = 0;

    friend bool operator==(const LineupPlayerFacts&, const LineupPlayerFacts&) = default;
};

struct LineupContext {
    // The squad: players who are not archived. A player of the lineup who is not here is checked
    // only by the rules that need no facts.
    std::map<PlayerId, LineupPlayerFacts> players;
    int yellowCardThreshold = kDefaultYellowCardThreshold;

    friend bool operator==(const LineupContext&, const LineupContext&) = default;
};

class ILineupRule {
public:
    ILineupRule() = default;
    virtual ~ILineupRule() = default;
    ILineupRule(const ILineupRule&) = delete;
    ILineupRule& operator=(const ILineupRule&) = delete;
    ILineupRule(ILineupRule&&) = delete;
    ILineupRule& operator=(ILineupRule&&) = delete;

    [[nodiscard]] virtual LineupRule rule() const noexcept = 0;
    [[nodiscard]] virtual LineupSeverity severity() const noexcept = 0;
    // Appends what it finds to `issues`.
    virtual void check(const Lineup& lineup, const LineupContext& context,
                       std::vector<LineupIssue>& issues) const = 0;
};

class LineupValidator {
public:
    // The rules of SPEC section 7.2.
    LineupValidator();
    explicit LineupValidator(std::vector<std::unique_ptr<ILineupRule>> rules);

    // Errors first, then warnings. Within each: issues of the whole lineup, then the slots in
    // order, then the bench; on one place in the order of the rules.
    [[nodiscard]] std::vector<LineupIssue> validate(const Lineup& lineup,
                                                    const LineupContext& context) const;

private:
    std::vector<std::unique_ptr<ILineupRule>> m_rules;
};

[[nodiscard]] bool hasErrors(std::span<const LineupIssue> issues) noexcept;

} // namespace fdss::core
