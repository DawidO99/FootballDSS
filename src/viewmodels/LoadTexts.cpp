#include "viewmodels/LoadTexts.hpp"

#include "viewmodels/IsoDates.hpp"

#include <QDate>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace fdss::viewmodels {

namespace {

using core::LoadReason;

bool has(const core::LoadVerdict& verdict, LoadReason reason) {
    return std::ranges::find(verdict.reasons, reason) != verdict.reasons.end();
}

QDate toQDate(core::Date date) {
    return QDate(static_cast<int>(date.year()),
                 static_cast<int>(static_cast<unsigned>(date.month())),
                 static_cast<int>(static_cast<unsigned>(date.day())));
}

} // namespace

QString LoadTexts::statusKey(core::LoadStatus status) {
    switch (status) {
    case core::LoadStatus::High:
        return QStringLiteral("high");
    case core::LoadStatus::Elevated:
        return QStringLiteral("elevated");
    case core::LoadStatus::Normal:
        return QStringLiteral("normal");
    case core::LoadStatus::Underloaded:
        return QStringLiteral("underloaded");
    case core::LoadStatus::NotEnoughData:
        break;
    }
    return QStringLiteral("notEnoughData");
}

QString LoadTexts::statusName(const core::LoadVerdict& verdict) {
    switch (verdict.status) {
    case core::LoadStatus::High:
        return tr("High load");
    case core::LoadStatus::Elevated:
        return tr("Elevated");
    case core::LoadStatus::Normal:
        return tr("In range");
    case core::LoadStatus::Underloaded:
        return tr("Underloaded");
    case core::LoadStatus::NotEnoughData:
        break;
    }
    const bool onlyTime = verdict.reasons == std::vector{LoadReason::ShortBaseline};
    return onlyTime ? tr("Building the baseline") : tr("Not enough data");
}

int LoadTexts::statusRank(core::LoadStatus status) {
    switch (status) {
    case core::LoadStatus::High:
        return 0;
    case core::LoadStatus::Elevated:
        return 1;
    case core::LoadStatus::Underloaded:
        return 2;
    case core::LoadStatus::Normal:
        return 3;
    case core::LoadStatus::NotEnoughData:
        break;
    }
    return 4;
}

QString LoadTexts::load(double value) { return QString::number(std::lround(value)); }

QString LoadTexts::decimal(double value, int decimals) {
    //: Decimal separator of the app language, used in the load ratio and monotony.
    return QString::number(value, 'f', decimals).replace(u'.', tr("."));
}

QString LoadTexts::explanation(const core::LoadAssessment& assessment,
                               const core::LoadThresholds& thresholds) {
    const core::LoadVerdict& verdict = assessment.verdict;
    const core::LoadIndicators& indicators = assessment.indicators;
    QStringList sentences;

    if (verdict.status == core::LoadStatus::NotEnoughData) {
        if (has(verdict, LoadReason::NoRpe)) {
            sentences << tr("No RPE entered yet.");
        }
        if (has(verdict, LoadReason::FewRpeDays)) {
            sentences << tr("Days with RPE: %1 of the %2 a status needs.")
                             .arg(indicators.rpeDays)
                             .arg(thresholds.minRpeDays);
        }
        if (has(verdict, LoadReason::ShortBaseline)) {
            sentences << tr("The baseline is still being built: a comparison needs %1 days since "
                            "the first entry.")
                             .arg(thresholds.baselineDays);
        }
        if (has(verdict, LoadReason::NoChronicLoad)) {
            sentences << tr("No load in the last weeks to compare with.");
        }
        return sentences.join(u' ');
    }

    const QString acute = load(indicators.acute);
    const QString chronic = load(indicators.chronic);
    const QString ratio = decimal(indicators.ratio.value_or(0.0), 2);
    if (has(verdict, LoadReason::RatioHigh)) {
        sentences << tr("Acute load %1 is %2 × the chronic load %3, above %4.")
                         .arg(acute, ratio, chronic, decimal(thresholds.ratioHigh, 1));
    }
    if (has(verdict, LoadReason::RatioElevated)) {
        sentences << tr("Acute load %1 is %2 × the chronic load %3, above %4.")
                         .arg(acute, ratio, chronic, decimal(thresholds.ratioElevated, 1));
    }
    if (has(verdict, LoadReason::AfterBreak)) {
        sentences << tr("The chronic load is low after a break (days without load: %1).")
                         .arg(indicators.breakDays.value_or(0));
    }
    if (has(verdict, LoadReason::MonotonyHigh)) {
        sentences << tr("Monotony %1 is above %2: the days of the last week were much alike.")
                         .arg(decimal(indicators.monotony.value_or(0.0), 2),
                              decimal(thresholds.monotonyHigh, 1));
    }
    if (has(verdict, LoadReason::RatioLow)) {
        sentences << tr("Acute load %1 is %2 × the chronic load %3, below %4: less than the "
                        "player is used to.")
                         .arg(acute, ratio, chronic, decimal(thresholds.ratioLow, 1));
    }
    if (has(verdict, LoadReason::RatioInRange)) {
        sentences << tr("Acute load %1 is %2 × the chronic load %3, between %4 and %5: in line "
                        "with what the player is used to.")
                         .arg(acute, ratio, chronic, decimal(thresholds.ratioLow, 1),
                              decimal(thresholds.ratioElevated, 1));
    }
    if (verdict.status == core::LoadStatus::High || verdict.status == core::LoadStatus::Elevated) {
        sentences << tr("A signal to talk to the player.");
    }
    return sentences.join(u' ');
}

QString LoadTexts::hint(const core::LoadAssessment& assessment) {
    const core::LoadVerdict& verdict = assessment.verdict;
    if (verdict.status != core::LoadStatus::NotEnoughData) {
        return {};
    }
    QStringList sentences;
    if (assessment.rpeDaysMissing > 0) {
        sentences << tr("Days with RPE still needed: %1.").arg(assessment.rpeDaysMissing);
    }
    if (has(verdict, LoadReason::ShortBaseline)) {
        sentences
            << tr("Full comparison from %1.").arg(IsoDates::displayText(assessment.baselineFrom));
    }
    if (sentences.isEmpty() && has(verdict, LoadReason::NoChronicLoad)) {
        sentences << tr("Enter the RPE of the next sessions.");
    }
    return sentences.join(u' ');
}

QString LoadTexts::missingRpe(const core::LoadAssessment& assessment,
                              const core::LoadThresholds& thresholds) {
    if (assessment.missingRpeSessions == 0 && assessment.missingRpeMatches == 0) {
        return {};
    }
    // Counted texts end in the number: no plural forms needed in any language.
    return tr("Without RPE in the last %1 days: sessions %2, matches %3.")
        .arg(thresholds.chronicDays)
        .arg(assessment.missingRpeSessions)
        .arg(assessment.missingRpeMatches);
}

QString LoadTexts::asOf(core::Date date) { return tr("As of %1").arg(IsoDates::displayText(date)); }

QString LoadTexts::chartDay(core::Date date) {
    //: Qt date format of a day on the load chart axis: day and month. dd.MM in Polish.
    return toQDate(date).toString(tr("MM/dd"));
}

} // namespace fdss::viewmodels
