#pragma once

#include "core/algorithms/LineupValidator.hpp"
#include "core/domain/Lineup.hpp"
#include "core/domain/Match.hpp"
#include "services/CalendarService.hpp"

#include <QCoreApplication>
#include <QString>

#include <optional>

namespace fdss::viewmodels {

// What the Pre-match screen shows for the lineup types (packet P12). Load wording follows SPEC
// section 7.1: a signal to talk to the player, never a diagnosis. Counted texts put the number
// after a colon, so they need no plural forms (P11 decision 13). Every text follows the language on
// the next call.
class LineupTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::LineupTexts)

public:
    // Independent of the language, for the views and the tests: startingEleven, goalkeeper,
    // unavailable, duplicate, highLoad, outOfPosition, missedSessions, yellowCards.
    [[nodiscard]] static QString ruleKey(core::LineupRule rule);
    // error or warning.
    [[nodiscard]] static QString severityKey(core::LineupSeverity severity);
    [[nodiscard]] static QString severityName(core::LineupSeverity severity);
    // The issue with the name of its player (empty for the issues of the whole lineup).
    [[nodiscard]] static QString issueText(const core::LineupIssue& issue,
                                           const QString& playerName);
    // The catalogue name, or the counted one marked as custom: "4-3-1-2 (custom)".
    [[nodiscard]] static QString formationText(const core::Lineup& lineup);
    // "Sat 07/11/2026 15:00 · Home · Liga".
    [[nodiscard]] static QString matchDetails(const core::Match& match);
    // "07/11/2026 · Rywal (home)", the text of the match field.
    [[nodiscard]] static QString matchOption(const services::CalendarMatch& match);
    // "Last meeting: won 2:1 on 10/10/2026 (away)." or that there is none.
    [[nodiscard]] static QString lastMeeting(const std::optional<core::Match>& match);
    // The XI or the bench: "In the XI", "On the bench", empty.
    [[nodiscard]] static QString placementName(const QString& placement);
};

} // namespace fdss::viewmodels
