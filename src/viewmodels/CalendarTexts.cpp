#include "viewmodels/CalendarTexts.hpp"

#include "viewmodels/QmlValues.hpp"

#include <chrono>

namespace fdss::viewmodels {

namespace {

const services::CalendarMatch* asMatch(const CalendarEntry& entry) {
    return std::get_if<services::CalendarMatch>(&entry);
}

const core::TrainingSession& asSession(const CalendarEntry& entry) {
    return std::get<core::TrainingSession>(entry);
}

} // namespace

QString CalendarTexts::venueName(core::Venue venue) {
    switch (venue) {
    case core::Venue::Home:
        return tr("Home");
    case core::Venue::Away:
        return tr("Away");
    case core::Venue::Neutral:
        return tr("Neutral");
    }
    return {};
}

QString CalendarTexts::sessionTypeName(core::SessionType type) {
    switch (type) {
    case core::SessionType::Training:
        return tr("Training");
    case core::SessionType::MatchPrep:
        return tr("Match preparation");
    case core::SessionType::Recovery:
        return tr("Recovery");
    case core::SessionType::Gym:
        return tr("Gym");
    case core::SessionType::Other:
        return tr("Other");
    }
    return {};
}

QString CalendarTexts::matchStatusName(core::MatchStatus status) {
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

QString CalendarTexts::dayName(core::Date date) {
    const std::chrono::weekday day{std::chrono::sys_days{date}};
    if (day == std::chrono::Monday) {
        return tr("Monday");
    }
    if (day == std::chrono::Tuesday) {
        return tr("Tuesday");
    }
    if (day == std::chrono::Wednesday) {
        return tr("Wednesday");
    }
    if (day == std::chrono::Thursday) {
        return tr("Thursday");
    }
    if (day == std::chrono::Friday) {
        return tr("Friday");
    }
    if (day == std::chrono::Saturday) {
        return tr("Saturday");
    }
    return tr("Sunday");
}

core::Date CalendarTexts::date(const CalendarEntry& entry) {
    const auto* match = asMatch(entry);
    return match ? match->match.date : asSession(entry).date;
}

std::optional<core::TimeOfDay> CalendarTexts::time(const CalendarEntry& entry) {
    const auto* match = asMatch(entry);
    return match ? match->match.kickoffTime : asSession(entry).startTime;
}

QString CalendarTexts::kind(const CalendarEntry& entry) {
    return asMatch(entry) ? QStringLiteral("match") : QStringLiteral("session");
}

qint64 CalendarTexts::id(const CalendarEntry& entry) {
    const auto* match = asMatch(entry);
    return match ? match->match.id.value : asSession(entry).id.value;
}

QString CalendarTexts::kindName(const CalendarEntry& entry) {
    return asMatch(entry) ? tr("Match") : tr("Session");
}

QString CalendarTexts::title(const CalendarEntry& entry) {
    const auto* match = asMatch(entry);
    return match ? QString::fromStdString(match->opponentName)
                 : sessionTypeName(asSession(entry).type);
}

QString CalendarTexts::details(const CalendarEntry& entry) {
    const auto* match = asMatch(entry);
    if (!match) {
        return tr("%1 min").arg(asSession(entry).durationMin);
    }
    const QString venue = venueName(match->match.venue);
    return match->match.competition ? QStringLiteral("%1 · %2").arg(
                                          venue, QString::fromStdString(*match->match.competition))
                                    : venue;
}

QString CalendarTexts::statusKey(const CalendarEntry& entry) {
    const auto* match = asMatch(entry);
    return match ? QString::fromUtf8(core::toDbText(match->match.status)) : QString{};
}

QString CalendarTexts::statusName(const CalendarEntry& entry) {
    const auto* match = asMatch(entry);
    return match ? matchStatusName(match->match.status) : QString{};
}

QVariantList CalendarTexts::venueOptions() { return optionsOf<core::Venue>(venueName); }

QVariantList CalendarTexts::sessionTypeOptions() {
    return optionsOf<core::SessionType>(sessionTypeName);
}

} // namespace fdss::viewmodels
