#include "core/algorithms/LineupRules.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>

namespace fdss::core {

namespace {

enum class Places { StartingEleven, WithBench };

LineupIssue issueOf(const ILineupRule& rule, std::optional<PlayerId> player = std::nullopt) {
    LineupIssue issue;
    issue.rule = rule.rule();
    issue.severity = rule.severity();
    issue.player = player;
    return issue;
}

// Calls `visit(player, issue)` for every place with a player, the slots in order and then the
// bench; `issue` has the rule, the severity, the player and the place filled in.
template <class Visit>
void forEachPlace(const Lineup& lineup, const ILineupRule& rule, Places places, Visit visit) {
    for (std::size_t i = 0; i < lineup.pitchSlots.size(); ++i) {
        if (const auto& player = lineup.pitchSlots[i].player) {
            LineupIssue issue = issueOf(rule, *player);
            issue.slot = static_cast<int>(i);
            visit(*player, issue);
        }
    }
    if (places == Places::StartingEleven) {
        return;
    }
    for (std::size_t i = 0; i < lineup.bench.size(); ++i) {
        LineupIssue issue = issueOf(rule, lineup.bench[i]);
        issue.benchIndex = static_cast<int>(i);
        visit(lineup.bench[i], issue);
    }
}

// The facts of the player, nullptr outside the squad.
const LineupPlayerFacts* factsOf(const LineupContext& context, PlayerId player) {
    const auto found = context.players.find(player);
    return found == context.players.end() ? nullptr : &found->second;
}

} // namespace

void StartingElevenRule::check(const Lineup& lineup, const LineupContext& /*context*/,
                               std::vector<LineupIssue>& issues) const {
    const int count = startingCount(lineup);
    if (count != kLineupSlots) {
        LineupIssue issue = issueOf(*this);
        issue.count = count;
        issues.push_back(issue);
    }
}

void GoalkeeperRule::check(const Lineup& lineup, const LineupContext& /*context*/,
                           std::vector<LineupIssue>& issues) const {
    const bool hasGoalkeeper = std::ranges::any_of(lineup.pitchSlots, [](const LineupSlot& slot) {
        return slot.position == Position::Gk && slot.player.has_value();
    });
    if (!hasGoalkeeper) {
        issues.push_back(issueOf(*this));
    }
}

void UnavailablePlayerRule::check(const Lineup& lineup, const LineupContext& context,
                                  std::vector<LineupIssue>& issues) const {
    forEachPlace(lineup, *this, Places::WithBench, [&](PlayerId player, LineupIssue issue) {
        const LineupPlayerFacts* facts = factsOf(context, player);
        if (facts && facts->statusOnMatchDay != PlayerStatus::Available) {
            issue.status = facts->statusOnMatchDay;
            issues.push_back(issue);
        }
    });
}

void DuplicatePlayerRule::check(const Lineup& lineup, const LineupContext& /*context*/,
                                std::vector<LineupIssue>& issues) const {
    std::set<PlayerId> seen;
    forEachPlace(lineup, *this, Places::WithBench, [&](PlayerId player, const LineupIssue& issue) {
        if (!seen.insert(player).second) {
            issues.push_back(issue);
        }
    });
}

void HighLoadRule::check(const Lineup& lineup, const LineupContext& context,
                         std::vector<LineupIssue>& issues) const {
    forEachPlace(lineup, *this, Places::WithBench, [&](PlayerId player, const LineupIssue& issue) {
        const LineupPlayerFacts* facts = factsOf(context, player);
        if (facts && facts->loadStatus == LoadStatus::High) {
            issues.push_back(issue);
        }
    });
}

void OutOfPositionRule::check(const Lineup& lineup, const LineupContext& context,
                              std::vector<LineupIssue>& issues) const {
    forEachPlace(lineup, *this, Places::StartingEleven, [&](PlayerId player, LineupIssue issue) {
        const LineupPlayerFacts* facts = factsOf(context, player);
        if (!facts) {
            return;
        }
        const Position position = lineup.pitchSlots[static_cast<std::size_t>(*issue.slot)].position;
        const bool nominal = facts->primaryPosition == position ||
                             std::ranges::find(facts->secondaryPositions, position) !=
                                 facts->secondaryPositions.end();
        if (!nominal) {
            issue.slotPosition = position;
            issues.push_back(issue);
        }
    });
}

void MissedSessionsRule::check(const Lineup& lineup, const LineupContext& context,
                               std::vector<LineupIssue>& issues) const {
    forEachPlace(lineup, *this, Places::WithBench, [&](PlayerId player, LineupIssue issue) {
        const LineupPlayerFacts* facts = factsOf(context, player);
        if (facts && facts->missedRecentSessions) {
            issue.count = kRecentSessions;
            issues.push_back(issue);
        }
    });
}

void YellowCardsRule::check(const Lineup& lineup, const LineupContext& context,
                            std::vector<LineupIssue>& issues) const {
    const int threshold = context.yellowCardThreshold;
    if (threshold < kMinYellowCardThreshold) {
        return;
    }
    forEachPlace(lineup, *this, Places::WithBench, [&](PlayerId player, LineupIssue issue) {
        const LineupPlayerFacts* facts = factsOf(context, player);
        if (facts && facts->yellowCards % threshold == threshold - 1) {
            issue.count = facts->yellowCards;
            issues.push_back(issue);
        }
    });
}

} // namespace fdss::core
