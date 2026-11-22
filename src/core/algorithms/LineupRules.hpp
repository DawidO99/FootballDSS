#pragma once

#include "core/algorithms/LineupValidator.hpp"

// One class per rule of SPEC section 7.2 (P12 decisions 5–8 and 3). Each looks at its own question
// only; LineupValidator puts the answers in order.

namespace fdss::core {

// Error: fewer than 11 players in the slots.
class StartingElevenRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::StartingEleven; }
    [[nodiscard]] LineupSeverity severity() const noexcept override {
        return LineupSeverity::Error;
    }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

// Error: no slot with the position GK holds a player. An outfield player there is a warning of
// OutOfPositionRule.
class GoalkeeperRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::Goalkeeper; }
    [[nodiscard]] LineupSeverity severity() const noexcept override {
        return LineupSeverity::Error;
    }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

// Error: a player of the XI or the bench is not available on the match date (injury, illness,
// suspension or absence).
class UnavailablePlayerRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::Unavailable; }
    [[nodiscard]] LineupSeverity severity() const noexcept override {
        return LineupSeverity::Error;
    }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

// Error: a player in more than one place; reported at every place after the first.
class DuplicatePlayerRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::Duplicate; }
    [[nodiscard]] LineupSeverity severity() const noexcept override {
        return LineupSeverity::Error;
    }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

// Warning: the load status (P11) of a player of the XI or the bench is High.
class HighLoadRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::HighLoad; }
    [[nodiscard]] LineupSeverity severity() const noexcept override {
        return LineupSeverity::Warning;
    }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

// Warning: the position of the slot is neither the primary nor a secondary position of its player.
// The XI only: the bench has no positions. A player without a primary position is not checked:
// PositionNotGivenRule says so instead.
class OutOfPositionRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::OutOfPosition; }
    [[nodiscard]] LineupSeverity severity() const noexcept override {
        return LineupSeverity::Warning;
    }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

// Info: a player of the XI has no primary position, so the position check does not apply (P19
// decision 6).
class PositionNotGivenRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::PositionNotGiven; }
    [[nodiscard]] LineupSeverity severity() const noexcept override { return LineupSeverity::Info; }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

// Warning: a player of the XI or the bench missed each of the last kRecentSessions sessions.
class MissedSessionsRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::MissedSessions; }
    [[nodiscard]] LineupSeverity severity() const noexcept override {
        return LineupSeverity::Warning;
    }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

// Warning: one more yellow card means a suspension. Cards accumulate up to a suspension and the
// count starts again, so the warning shows when cards mod threshold == threshold − 1.
class YellowCardsRule final : public ILineupRule {
public:
    [[nodiscard]] LineupRule rule() const noexcept override { return LineupRule::YellowCards; }
    [[nodiscard]] LineupSeverity severity() const noexcept override {
        return LineupSeverity::Warning;
    }
    void check(const Lineup& lineup, const LineupContext& context,
               std::vector<LineupIssue>& issues) const override;
};

} // namespace fdss::core
