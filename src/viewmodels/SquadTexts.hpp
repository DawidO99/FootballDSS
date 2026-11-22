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
    // A primary position that is not given (P19): "—" in every language, and "Position not given".
    [[nodiscard]] static QString positionCode(const std::optional<core::Position>& position);
    [[nodiscard]] static QString positionName(const std::optional<core::Position>& position);
    // The squad table shows this many secondary codes at most, then "+N" (P19).
    static constexpr int kShownSecondaryCodes = 3;
    // [{ code, highlighted }] of at most kShownSecondaryCodes secondary positions; highlighted: in
    // the active group. The highlighted ones come first, otherwise the stored order stays.
    [[nodiscard]] static QVariantList
    secondaryPositionCodes(const core::Player& player,
                           std::optional<core::PositionGroup> activeGroup);
    // How many secondary positions secondaryPositionCodes() leaves out.
    [[nodiscard]] static int hiddenSecondaryCount(const core::Player& player);
    // Why the player is in the active group when only a secondary position puts them there;
    // otherwise the full names of the primary and the secondary positions.
    [[nodiscard]] static QString positionTooltip(const core::Player& player,
                                                 std::optional<core::PositionGroup> activeGroup);
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
    // positionOptions() after "—" (value "") for a primary position that may be not given.
    [[nodiscard]] static QVariantList optionalPositionOptions();
    [[nodiscard]] static QVariantList footOptions();
    [[nodiscard]] static QVariantList availabilityTypeOptions();
};

} // namespace fdss::viewmodels
