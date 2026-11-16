#include "viewmodels/LineupTexts.hpp"

#include "core/algorithms/Formations.hpp"
#include "viewmodels/CalendarTexts.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QStringList>

namespace fdss::viewmodels {

QString LineupTexts::ruleKey(core::LineupRule rule) {
    switch (rule) {
    case core::LineupRule::StartingEleven:
        return QStringLiteral("startingEleven");
    case core::LineupRule::Goalkeeper:
        return QStringLiteral("goalkeeper");
    case core::LineupRule::Unavailable:
        return QStringLiteral("unavailable");
    case core::LineupRule::Duplicate:
        return QStringLiteral("duplicate");
    case core::LineupRule::HighLoad:
        return QStringLiteral("highLoad");
    case core::LineupRule::OutOfPosition:
        return QStringLiteral("outOfPosition");
    case core::LineupRule::MissedSessions:
        return QStringLiteral("missedSessions");
    case core::LineupRule::YellowCards:
        return QStringLiteral("yellowCards");
    }
    return {};
}

QString LineupTexts::severityKey(core::LineupSeverity severity) {
    return severity == core::LineupSeverity::Error ? QStringLiteral("error")
                                                   : QStringLiteral("warning");
}

QString LineupTexts::severityName(core::LineupSeverity severity) {
    return severity == core::LineupSeverity::Error ? tr("Error") : tr("Warning");
}

QString LineupTexts::issueText(const core::LineupIssue& issue, const QString& playerName) {
    switch (issue.rule) {
    case core::LineupRule::StartingEleven:
        return tr("Players in the starting XI: %1 of 11.").arg(issue.count);
    case core::LineupRule::Goalkeeper:
        return tr("No goalkeeper: put a player in the GK slot.");
    case core::LineupRule::Unavailable:
        return tr("%1 is not available on the match day: %2.")
            .arg(playerName, SquadTexts::statusName(issue.status, false).toLower());
    case core::LineupRule::Duplicate:
        return tr("%1 is in the lineup more than once.").arg(playerName);
    case core::LineupRule::HighLoad:
        return tr("%1 has a high load. A signal to talk to the player.").arg(playerName);
    case core::LineupRule::OutOfPosition:
        return tr("%1 plays out of position: %2.")
            .arg(playerName, issue.slotPosition
                                 ? SquadTexts::positionName(*issue.slotPosition).toLower()
                                 : QString{});
    case core::LineupRule::MissedSessions:
        return tr("%1 missed each of the last training sessions: %2.")
            .arg(playerName)
            .arg(issue.count);
    case core::LineupRule::YellowCards:
        return tr("%1 has yellow cards: %2. One more means a suspension.")
            .arg(playerName)
            .arg(issue.count);
    }
    return {};
}

QString LineupTexts::formationText(const core::Lineup& lineup) {
    const QString name = QString::fromStdString(lineup.formation);
    return core::matchingFormation(lineup.pitchSlots) ? name : tr("%1 (custom)").arg(name);
}

QString LineupTexts::matchDetails(const core::Match& match) {
    QStringList parts;
    QString when =
        CalendarTexts::dayName(match.date) + QLatin1Char(' ') + IsoDates::displayText(match.date);
    if (match.kickoffTime) {
        when += QLatin1Char(' ') + IsoDates::timeToIso(match.kickoffTime);
    }
    parts.append(when);
    parts.append(CalendarTexts::venueName(match.venue));
    if (match.competition) {
        parts.append(QString::fromStdString(*match.competition));
    }
    return parts.join(QStringLiteral(" · "));
}

QString LineupTexts::matchOption(const services::CalendarMatch& match) {
    return tr("%1 · %2 (%3)")
        .arg(IsoDates::displayText(match.match.date), QString::fromStdString(match.opponentName),
             CalendarTexts::venueName(match.match.venue).toLower());
}

QString LineupTexts::lastMeeting(const std::optional<core::Match>& match) {
    if (!match || !match->goalsFor || !match->goalsAgainst) {
        return tr("No earlier match against this opponent.");
    }
    const int goalsFor = *match->goalsFor;
    const int goalsAgainst = *match->goalsAgainst;
    const QString date = IsoDates::displayText(match->date);
    const QString venue = CalendarTexts::venueName(match->venue).toLower();
    if (goalsFor > goalsAgainst) {
        return tr("Last meeting: won %1:%2 on %3 (%4).")
            .arg(goalsFor)
            .arg(goalsAgainst)
            .arg(date, venue);
    }
    if (goalsFor < goalsAgainst) {
        return tr("Last meeting: lost %1:%2 on %3 (%4).")
            .arg(goalsFor)
            .arg(goalsAgainst)
            .arg(date, venue);
    }
    return tr("Last meeting: drew %1:%2 on %3 (%4).")
        .arg(goalsFor)
        .arg(goalsAgainst)
        .arg(date, venue);
}

QString LineupTexts::placementName(const QString& placement) {
    if (placement == QLatin1String("xi")) {
        return tr("In the XI");
    }
    if (placement == QLatin1String("bench")) {
        return tr("On the bench");
    }
    return {};
}

} // namespace fdss::viewmodels
