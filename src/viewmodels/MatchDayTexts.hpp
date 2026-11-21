#pragma once

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/MatchEvent.hpp"

#include <QCoreApplication>
#include <QString>

#include <optional>

namespace fdss::viewmodels {

// What the Match Day screen shows for the core types (packet P13). Parts of a match are "halves"
// with 2 parts and "periods" otherwise. Every text follows the language on the next call.
class MatchDayTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::MatchDayTexts)

public:
    // The football minute of an event: "1'", "45'", "45+3'", "30+2'" (P13 decisions).
    [[nodiscard]] static QString minuteText(int minute, std::optional<int> addedTime);
    // The same without the apostrophe, for the minute field: "23", "45+2".
    [[nodiscard]] static QString minuteFieldText(const core::MatchTime& time);
    // The clock: "23:41", in stoppage time "45+2:10" (the end of the part, then the time over it).
    [[nodiscard]] static QString clockText(const core::ClockReading& reading, int periodLength);
    // notStarted, running, break, finished.
    [[nodiscard]] static QString phaseKey(core::MatchPhase phase);
    // "1st half", "2nd period"…
    [[nodiscard]] static QString periodName(int period, int periods);
    // "Before kick-off", "1st half", "Half time", "Break", "Full time".
    [[nodiscard]] static QString phaseText(const core::ClockReading& reading, int periods);
    // "Start 1st half", "End 2nd period"… The end of the last part is "End match"; empty after it.
    [[nodiscard]] static QString clockActionText(std::optional<core::ClockStep> step, int periods);
    // "start of the 1st half"…, for undo.
    [[nodiscard]] static QString clockStepName(core::ClockStep step, int periods);
    // "end of the match", or "early end of the match".
    [[nodiscard]] static QString finishName(bool early);
    // "2 × 45 min".
    [[nodiscard]] static QString formatText(core::MatchFormat format);
    // goal, opponentGoal, yellow, red, secondYellow, sub, assist; note.
    [[nodiscard]] static QString eventKey(core::MatchEventType type, bool secondYellow);
    [[nodiscard]] static QString eventName(core::MatchEventType type, bool secondYellow);
    // The icon of Icon.qml: goal, card, swap; note.
    [[nodiscard]] static QString eventIcon(core::MatchEventType type);
    [[nodiscard]] static QString noteName();
    // Live tactics (P13b): "Change of shape", and "4-4-2 → 4-2-3-1" or, for the same name,
    // "4-4-2: positions changed".
    [[nodiscard]] static QString tacticChangeName();
    [[nodiscard]] static QString tacticChangeText(const QString& from, const QString& to);
    // The live shape (core::liveFormationName()), "(custom)" for an eleven not in the catalogue.
    [[nodiscard]] static QString shapeText(const core::Lineup& shape);
    // onPitch, bench, substitutedOff, sentOff.
    [[nodiscard]] static QString playerStateKey(core::PlayerMatchState state);
    [[nodiscard]] static QString playerStateName(core::PlayerMatchState state);
    [[nodiscard]] static QString statusName(core::MatchStatus status);
};

} // namespace fdss::viewmodels
