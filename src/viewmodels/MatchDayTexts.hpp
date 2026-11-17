#pragma once

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/MatchEvent.hpp"

#include <QCoreApplication>
#include <QString>

namespace fdss::viewmodels {

// What the Match Day screen shows for the core types (packet P13). Every text follows the language
// on the next call.
class MatchDayTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::MatchDayTexts)

public:
    // The football minute of an event: "1'", "45'", "45+3'", "90+2'" (P13 decision 4).
    [[nodiscard]] static QString minuteText(int minute, std::optional<int> addedTime);
    // The clock: "23:41", in stoppage time "45+2:10".
    [[nodiscard]] static QString clockText(const core::ClockReading& reading, int halfLength);
    // notStarted, firstHalf, halfTime, secondHalf, finished.
    [[nodiscard]] static QString phaseKey(core::MatchPhase phase);
    [[nodiscard]] static QString phaseName(core::MatchPhase phase);
    // "Start 1st half"…; empty after the end.
    [[nodiscard]] static QString clockActionText(std::optional<core::ClockStep> step);
    [[nodiscard]] static QString clockStepName(core::ClockStep step);
    // goal, opponentGoal, yellow, red, secondYellow, sub, assist.
    [[nodiscard]] static QString eventKey(core::MatchEventType type, bool secondYellow);
    [[nodiscard]] static QString eventName(core::MatchEventType type, bool secondYellow);
    // The icon of Icon.qml: goal, card, sub.
    [[nodiscard]] static QString eventIcon(core::MatchEventType type);
    // onPitch, bench, substitutedOff, sentOff.
    [[nodiscard]] static QString playerStateKey(core::PlayerMatchState state);
    [[nodiscard]] static QString playerStateName(core::PlayerMatchState state);
    [[nodiscard]] static QString statusName(core::MatchStatus status);
};

} // namespace fdss::viewmodels
