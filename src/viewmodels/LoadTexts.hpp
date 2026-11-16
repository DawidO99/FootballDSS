#pragma once

#include "core/algorithms/Load.hpp"
#include "core/domain/DateTypes.hpp"

#include <QCoreApplication>
#include <QString>

namespace fdss::viewmodels {

// What the load screens show for the results of core/algorithms/Load (SPEC section 7.1). A status
// is a signal to talk to the player, never a diagnosis: no text here speaks of injuries or risk.
// Every text follows the language on the next call, so a viewmodel asks again on
// QEvent::LanguageChange.
class LoadTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::LoadTexts)

public:
    // What LoadStatusBadge takes: high, elevated, normal, underloaded or notEnoughData.
    [[nodiscard]] static QString statusKey(core::LoadStatus status);
    // The word on the badge. Grey is "Building the baseline" while only the time is missing.
    [[nodiscard]] static QString statusName(const core::LoadVerdict& verdict);
    // The word in the team table, short enough for its status column at the minimum window in
    // every language (P11 review); statusName() goes to its tooltip and accessible name.
    [[nodiscard]] static QString statusShortName(core::LoadStatus status);
    // What drove a status, independent of the language: ratioHigh, afterBreak…
    [[nodiscard]] static QString reasonKey(core::LoadReason reason);
    // Most to least urgent: 0 high … 4 not enough data. The order of the status sort.
    [[nodiscard]] static int statusRank(core::LoadStatus status);

    // Whole arbitrary units.
    [[nodiscard]] static QString load(double value);
    // With the decimal separator of the app language.
    [[nodiscard]] static QString decimal(double value, int decimals);

    // Why the status is what it is: the numbers that drove it, and for high and elevated "A signal
    // to talk to the player."
    [[nodiscard]] static QString explanation(const core::LoadAssessment& assessment,
                                             const core::LoadThresholds& thresholds);
    // What is missing for a status, e.g. "Days with RPE still needed: 9." Empty when nothing is.
    // Counted texts put the number after a colon, so they need no plural forms in any language.
    [[nodiscard]] static QString hint(const core::LoadAssessment& assessment);
    // Sessions and matches without RPE in the last chronicDays days. Empty when there are none.
    [[nodiscard]] static QString missingRpe(const core::LoadAssessment& assessment,
                                            const core::LoadThresholds& thresholds);
    // "As of 31/10/2026".
    [[nodiscard]] static QString asOf(core::Date date);
    // A day and month: the chart axis, and the date of a row of the team table that differs from
    // the date of the table.
    [[nodiscard]] static QString chartDay(core::Date date);
};

} // namespace fdss::viewmodels
