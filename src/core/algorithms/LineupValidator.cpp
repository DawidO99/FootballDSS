#include "core/algorithms/LineupValidator.hpp"

#include "core/algorithms/LineupRules.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

namespace fdss::core {

namespace {

// The order of the issues: severity, then the place (the whole lineup, the slots, the bench), then
// the rule.
auto orderKey(const LineupIssue& issue) {
    const int placeKind = issue.slot ? 1 : issue.benchIndex ? 2 : 0;
    const int placeIndex = issue.slot ? *issue.slot : issue.benchIndex.value_or(0);
    return std::tuple{issue.severity, placeKind, placeIndex, issue.rule};
}

} // namespace

LineupValidator::LineupValidator() {
    m_rules.push_back(std::make_unique<StartingElevenRule>());
    m_rules.push_back(std::make_unique<GoalkeeperRule>());
    m_rules.push_back(std::make_unique<UnavailablePlayerRule>());
    m_rules.push_back(std::make_unique<DuplicatePlayerRule>());
    m_rules.push_back(std::make_unique<HighLoadRule>());
    m_rules.push_back(std::make_unique<OutOfPositionRule>());
    m_rules.push_back(std::make_unique<MissedSessionsRule>());
    m_rules.push_back(std::make_unique<YellowCardsRule>());
    m_rules.push_back(std::make_unique<PositionNotGivenRule>());
}

LineupValidator::LineupValidator(std::vector<std::unique_ptr<ILineupRule>> rules)
    : m_rules(std::move(rules)) {}

std::vector<LineupIssue> LineupValidator::validate(const Lineup& lineup,
                                                   const LineupContext& context) const {
    std::vector<LineupIssue> issues;
    for (const auto& rule : m_rules) {
        rule->check(lineup, context, issues);
    }
    std::ranges::stable_sort(issues, [](const LineupIssue& a, const LineupIssue& b) {
        return orderKey(a) < orderKey(b);
    });
    return issues;
}

bool hasErrors(std::span<const LineupIssue> issues) noexcept {
    return std::ranges::any_of(
        issues, [](const LineupIssue& issue) { return issue.severity == LineupSeverity::Error; });
}

} // namespace fdss::core
