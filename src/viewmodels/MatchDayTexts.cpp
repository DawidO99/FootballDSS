#include "viewmodels/MatchDayTexts.hpp"

#include "core/algorithms/Formations.hpp"

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

QString MatchDayTexts::minuteFieldText(const core::MatchTime& time) {
    const core::DisplayMinute shown = core::displayMinute(time);
    if (shown.added) {
        return QStringLiteral("%1+%2").arg(shown.minute).arg(*shown.added);
    }
    return QString::number(shown.minute);
}

QString MatchDayTexts::clockText(const core::ClockReading& reading, int periodLength) {
    if (reading.stoppage > std::chrono::seconds{0}) {
        const QString over = minutesSeconds(reading.stoppage);
        // "45+2:10": one digit for the minutes under 10.
        return QStringLiteral("%1+").arg(reading.period * periodLength) +
               (reading.stoppage < std::chrono::minutes{10} ? over.mid(1) : over);
    }
    return minutesSeconds(reading.regular);
}

QString MatchDayTexts::phaseKey(core::MatchPhase phase) {
    switch (phase) {
    case core::MatchPhase::NotStarted:
        return QStringLiteral("notStarted");
    case core::MatchPhase::Running:
        return QStringLiteral("running");
    case core::MatchPhase::Break:
        return QStringLiteral("break");
    case core::MatchPhase::Finished:
        return QStringLiteral("finished");
    }
    return {};
}

QString MatchDayTexts::periodName(int period, int periods) {
    if (periods == 2) {
        return period == 1 ? tr("1st half") : tr("2nd half");
    }
    switch (period) {
    case 1:
        return tr("1st period");
    case 2:
        return tr("2nd period");
    case 3:
        return tr("3rd period");
    default:
        return tr("4th period");
    }
}

QString MatchDayTexts::phaseText(const core::ClockReading& reading, int periods) {
    switch (reading.phase) {
    case core::MatchPhase::NotStarted:
        return tr("Before kick-off");
    case core::MatchPhase::Running:
        return periodName(reading.period, periods);
    case core::MatchPhase::Break:
        return periods == 2 ? tr("Half time") : tr("Break");
    case core::MatchPhase::Finished:
        return tr("Full time");
    }
    return {};
}

QString MatchDayTexts::clockActionText(std::optional<core::ClockStep> step, int periods) {
    if (!step) {
        return {};
    }
    const bool start = step->kind == core::ClockStepKind::Start;
    if (!start && step->period >= periods) {
        return tr("End match");
    }
    if (periods == 2) {
        if (step->period == 1) {
            return start ? tr("Start 1st half") : tr("End 1st half");
        }
        return tr("Start 2nd half");
    }
    switch (step->period) {
    case 1:
        return start ? tr("Start 1st period") : tr("End 1st period");
    case 2:
        return start ? tr("Start 2nd period") : tr("End 2nd period");
    case 3:
        return start ? tr("Start 3rd period") : tr("End 3rd period");
    default:
        return tr("Start 4th period");
    }
}

QString MatchDayTexts::clockStepName(core::ClockStep step, int periods) {
    const bool start = step.kind == core::ClockStepKind::Start;
    if (periods == 2) {
        if (step.period == 1) {
            return start ? tr("start of the 1st half") : tr("end of the 1st half");
        }
        return start ? tr("start of the 2nd half") : tr("end of the 2nd half");
    }
    switch (step.period) {
    case 1:
        return start ? tr("start of the 1st period") : tr("end of the 1st period");
    case 2:
        return start ? tr("start of the 2nd period") : tr("end of the 2nd period");
    case 3:
        return start ? tr("start of the 3rd period") : tr("end of the 3rd period");
    default:
        return start ? tr("start of the 4th period") : tr("end of the 4th period");
    }
}

QString MatchDayTexts::finishName(bool early) {
    return early ? tr("early end of the match") : tr("end of the match");
}

QString MatchDayTexts::formatText(core::MatchFormat format) {
    return tr("%1 × %2 min").arg(format.periods).arg(format.periodLength);
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

QString MatchDayTexts::noteName() { return tr("Note"); }

QString MatchDayTexts::tacticChangeName() { return tr("Change of shape"); }

QString MatchDayTexts::shapeText(const core::Lineup& shape) {
    const QString name = QString::fromStdString(core::liveFormationName(shape));
    return core::isCustomLiveShape(shape) ? tr("%1 (custom)").arg(name) : name;
}

QString MatchDayTexts::tacticChangeText(const QString& from, const QString& to) {
    if (from == to) {
        return tr("%1: positions changed").arg(to);
    }
    return tr("%1 → %2").arg(from, to);
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
