#include "viewmodels/MatchDayTexts.hpp"

#include <chrono>

namespace fdss::viewmodels {

namespace {

QString twoDigits(long long value) {
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

// "mm:ss" of a duration; minutes may exceed 59.
QString minutesSeconds(std::chrono::seconds duration) {
    const long long total = duration.count();
    return twoDigits(total / 60) + QLatin1Char(':') + twoDigits(total % 60);
}

} // namespace

QString MatchDayTexts::minuteText(int minute, std::optional<int> addedTime) {
    const core::DisplayMinute shown = core::displayMinute({minute, addedTime});
    if (shown.added) {
        return QStringLiteral("%1+%2'").arg(shown.minute).arg(*shown.added);
    }
    return QStringLiteral("%1'").arg(shown.minute);
}

QString MatchDayTexts::clockText(const core::ClockReading& reading, int halfLength) {
    if (reading.stoppage > std::chrono::seconds{0}) {
        const int halfEnd =
            reading.phase == core::MatchPhase::SecondHalf ? 2 * halfLength : halfLength;
        return QStringLiteral("%1+").arg(halfEnd) +
               minutesSeconds(reading.stoppage)
                   .mid(reading.stoppage < std::chrono::minutes{10} ? 1 : 0);
    }
    return minutesSeconds(reading.regular);
}

QString MatchDayTexts::phaseKey(core::MatchPhase phase) {
    switch (phase) {
    case core::MatchPhase::NotStarted:
        return QStringLiteral("notStarted");
    case core::MatchPhase::FirstHalf:
        return QStringLiteral("firstHalf");
    case core::MatchPhase::HalfTime:
        return QStringLiteral("halfTime");
    case core::MatchPhase::SecondHalf:
        return QStringLiteral("secondHalf");
    case core::MatchPhase::Finished:
        return QStringLiteral("finished");
    }
    return {};
}

QString MatchDayTexts::phaseName(core::MatchPhase phase) {
    switch (phase) {
    case core::MatchPhase::NotStarted:
        return tr("Before kick-off");
    case core::MatchPhase::FirstHalf:
        return tr("1st half");
    case core::MatchPhase::HalfTime:
        return tr("Half time");
    case core::MatchPhase::SecondHalf:
        return tr("2nd half");
    case core::MatchPhase::Finished:
        return tr("Full time");
    }
    return {};
}

QString MatchDayTexts::clockActionText(std::optional<core::ClockStep> step) {
    if (!step) {
        return {};
    }
    switch (*step) {
    case core::ClockStep::FirstHalfStart:
        return tr("Start 1st half");
    case core::ClockStep::FirstHalfEnd:
        return tr("End 1st half");
    case core::ClockStep::SecondHalfStart:
        return tr("Start 2nd half");
    case core::ClockStep::MatchEnd:
        return tr("End match");
    }
    return {};
}

QString MatchDayTexts::clockStepName(core::ClockStep step) {
    switch (step) {
    case core::ClockStep::FirstHalfStart:
        return tr("start of the 1st half");
    case core::ClockStep::FirstHalfEnd:
        return tr("end of the 1st half");
    case core::ClockStep::SecondHalfStart:
        return tr("start of the 2nd half");
    case core::ClockStep::MatchEnd:
        return tr("end of the match");
    }
    return {};
}

QString MatchDayTexts::eventKey(core::MatchEventType type, bool secondYellow) {
    switch (type) {
    case core::MatchEventType::Goal:
        return QStringLiteral("goal");
    case core::MatchEventType::Assist:
        return QStringLiteral("assist");
    case core::MatchEventType::Yellow:
        return QStringLiteral("yellow");
    case core::MatchEventType::Red:
        return secondYellow ? QStringLiteral("secondYellow") : QStringLiteral("red");
    case core::MatchEventType::Sub:
        return QStringLiteral("sub");
    case core::MatchEventType::OpponentGoal:
        return QStringLiteral("opponentGoal");
    }
    return {};
}

QString MatchDayTexts::eventName(core::MatchEventType type, bool secondYellow) {
    switch (type) {
    case core::MatchEventType::Goal:
        return tr("Goal");
    case core::MatchEventType::Assist:
        return tr("Assist");
    case core::MatchEventType::Yellow:
        return tr("Yellow card");
    case core::MatchEventType::Red:
        return secondYellow ? tr("Second yellow, red card") : tr("Red card");
    case core::MatchEventType::Sub:
        return tr("Substitution");
    case core::MatchEventType::OpponentGoal:
        return tr("Opponent goal");
    }
    return {};
}

QString MatchDayTexts::eventIcon(core::MatchEventType type) {
    switch (type) {
    case core::MatchEventType::Goal:
    case core::MatchEventType::OpponentGoal:
    case core::MatchEventType::Assist:
        return QStringLiteral("goal");
    case core::MatchEventType::Yellow:
    case core::MatchEventType::Red:
        return QStringLiteral("card");
    case core::MatchEventType::Sub:
        return QStringLiteral("swap");
    }
    return {};
}

QString MatchDayTexts::playerStateKey(core::PlayerMatchState state) {
    switch (state) {
    case core::PlayerMatchState::OnPitch:
        return QStringLiteral("onPitch");
    case core::PlayerMatchState::Bench:
        return QStringLiteral("bench");
    case core::PlayerMatchState::SubstitutedOff:
        return QStringLiteral("substitutedOff");
    case core::PlayerMatchState::SentOff:
        return QStringLiteral("sentOff");
    }
    return {};
}

QString MatchDayTexts::playerStateName(core::PlayerMatchState state) {
    switch (state) {
    case core::PlayerMatchState::OnPitch:
        return tr("On the pitch");
    case core::PlayerMatchState::Bench:
        return tr("On the bench");
    case core::PlayerMatchState::SubstitutedOff:
        return tr("Substituted");
    case core::PlayerMatchState::SentOff:
        return tr("Sent off");
    }
    return {};
}

QString MatchDayTexts::statusName(core::MatchStatus status) {
    switch (status) {
    case core::MatchStatus::Planned:
        return tr("Planned");
    case core::MatchStatus::Live:
        return tr("Live");
    case core::MatchStatus::Finished:
        return tr("Finished");
    }
    return {};
}

} // namespace fdss::viewmodels
