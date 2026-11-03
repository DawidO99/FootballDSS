#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/TrainingSession.hpp"
#include "services/CalendarService.hpp"

#include <QCoreApplication>
#include <QString>
#include <QVariantList>

#include <optional>
#include <variant>

namespace fdss::viewmodels {

// One item of the calendar.
using CalendarEntry = std::variant<services::CalendarMatch, core::TrainingSession>;

// What the calendar screens show for the core types. Every text follows the language on the next
// call, so a model asks again on QEvent::LanguageChange.
class CalendarTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::CalendarTexts)

public:
    [[nodiscard]] static QString venueName(core::Venue venue);
    [[nodiscard]] static QString sessionTypeName(core::SessionType type);
    [[nodiscard]] static QString matchStatusName(core::MatchStatus status);
    [[nodiscard]] static QString dayName(core::Date date);

    [[nodiscard]] static core::Date date(const CalendarEntry& entry);
    [[nodiscard]] static std::optional<core::TimeOfDay> time(const CalendarEntry& entry);
    // match or session.
    [[nodiscard]] static QString kind(const CalendarEntry& entry);
    [[nodiscard]] static qint64 id(const CalendarEntry& entry);
    // The word for kind().
    [[nodiscard]] static QString kindName(const CalendarEntry& entry);
    // The opponent of a match, the type of a session.
    [[nodiscard]] static QString title(const CalendarEntry& entry);
    // The venue and the competition of a match, the duration of a session.
    [[nodiscard]] static QString details(const CalendarEntry& entry);
    // The database text of the status of a match; empty for a session, which has no status.
    [[nodiscard]] static QString statusKey(const CalendarEntry& entry);
    [[nodiscard]] static QString statusName(const CalendarEntry& entry);

    // [{ value, text }] for SelectField, the value being the database text.
    [[nodiscard]] static QVariantList venueOptions();
    [[nodiscard]] static QVariantList sessionTypeOptions();
};

} // namespace fdss::viewmodels
