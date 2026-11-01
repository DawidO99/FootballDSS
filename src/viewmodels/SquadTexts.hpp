#pragma once

#include "core/domain/Availability.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/Player.hpp"

#include <QCoreApplication>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <optional>

namespace fdss::viewmodels {

// What the squad screens show for the core types. Every text follows the language on the next call,
// so a viewmodel asks again on QEvent::LanguageChange.
class SquadTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::SquadTexts)

public:
    // International in every language: GK, RB… (P08 decision 9).
    [[nodiscard]] static QString positionCode(core::Position position);
    [[nodiscard]] static QString positionName(core::Position position);
    [[nodiscard]] static QString footName(core::PreferredFoot foot);
    [[nodiscard]] static QString availabilityTypeName(core::AvailabilityType type);

    // What PlayerStatusBadge takes: available, injured, ill, suspended, absent or archived. An
    // archived player has no status of today (P08 decision 4).
    [[nodiscard]] static QString statusKey(core::PlayerStatus status, bool archived);
    [[nodiscard]] static QString statusName(core::PlayerStatus status, bool archived);

    // "First Last".
    [[nodiscard]] static QString displayName(const core::Player& player);
    // The current year minus the birth year: only the year is stored. Null without a birth year.
    [[nodiscard]] static QVariant age(const std::optional<int>& birthYear, int currentYear);

    // [{ value, text }] for SelectField, the value being the database text.
    [[nodiscard]] static QVariantList positionOptions();
    [[nodiscard]] static QVariantList footOptions();
    [[nodiscard]] static QVariantList availabilityTypeOptions();
};

} // namespace fdss::viewmodels
