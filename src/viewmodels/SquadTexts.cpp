#include "viewmodels/SquadTexts.hpp"

#include "viewmodels/QmlValues.hpp"

#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <ranges>
#include <vector>

namespace fdss::viewmodels {

QString SquadTexts::positionCode(core::Position position) {
    return QString::fromUtf8(core::toDbText(position)).toUpper();
}

QString SquadTexts::positionCode(const std::optional<core::Position>& position) {
    return position ? positionCode(*position) : QStringLiteral("\u2014");
}

QString SquadTexts::positionName(const std::optional<core::Position>& position) {
    return position ? positionName(*position) : tr("Position not given");
}

QString SquadTexts::positionName(core::Position position) {
    switch (position) {
    case core::Position::Gk:
        return tr("Goalkeeper");
    case core::Position::Rb:
        return tr("Right back");
    case core::Position::Cb:
        return tr("Centre back");
    case core::Position::Lb:
        return tr("Left back");
    case core::Position::Rwb:
        return tr("Right wing-back");
    case core::Position::Lwb:
        return tr("Left wing-back");
    case core::Position::Dm:
        return tr("Defensive midfielder");
    case core::Position::Cm:
        return tr("Central midfielder");
    case core::Position::Am:
        return tr("Attacking midfielder");
    case core::Position::Rm:
        return tr("Right midfielder");
    case core::Position::Lm:
        return tr("Left midfielder");
    case core::Position::Rw:
        return tr("Right winger");
    case core::Position::Lw:
        return tr("Left winger");
    case core::Position::St:
        return tr("Striker");
    }
    return {};
}

QString SquadTexts::footName(core::PreferredFoot foot) {
    switch (foot) {
    case core::PreferredFoot::Left:
        return tr("Left");
    case core::PreferredFoot::Right:
        return tr("Right");
    case core::PreferredFoot::Both:
        return tr("Both");
    case core::PreferredFoot::Unknown:
        return tr("Not known");
    }
    return {};
}

QString SquadTexts::availabilityTypeName(core::AvailabilityType type) {
    switch (type) {
    case core::AvailabilityType::Injury:
        return tr("Injury");
    case core::AvailabilityType::Illness:
        return tr("Illness");
    case core::AvailabilityType::Suspension:
        return tr("Suspension");
    case core::AvailabilityType::Absence:
        return tr("Absence");
    }
    return {};
}

QString SquadTexts::statusKey(core::PlayerStatus status, bool archived) {
    if (archived) {
        return QStringLiteral("archived");
    }
    switch (status) {
    case core::PlayerStatus::Available:
        return QStringLiteral("available");
    case core::PlayerStatus::Injured:
        return QStringLiteral("injured");
    case core::PlayerStatus::Ill:
        return QStringLiteral("ill");
    case core::PlayerStatus::Suspended:
        return QStringLiteral("suspended");
    case core::PlayerStatus::Absent:
        return QStringLiteral("absent");
    }
    return {};
}

QString SquadTexts::statusName(core::PlayerStatus status, bool archived) {
    if (archived) {
        return tr("Archived");
    }
    switch (status) {
    case core::PlayerStatus::Available:
        return tr("Available");
    case core::PlayerStatus::Injured:
        return tr("Injured");
    case core::PlayerStatus::Ill:
        return tr("Ill");
    case core::PlayerStatus::Suspended:
        return tr("Suspended");
    case core::PlayerStatus::Absent:
        return tr("Absent");
    }
    return {};
}

QString SquadTexts::displayName(const core::Player& player) {
    return QString::fromStdString(player.firstName + ' ' + player.lastName);
}

QVariant SquadTexts::age(const std::optional<int>& birthYear, int currentYear) {
    return birthYear ? QVariant(currentYear - *birthYear) : QVariant{};
}

QVariantList SquadTexts::secondaryPositionCodes(const core::Player& player,
                                                std::optional<core::PositionGroup> activeGroup) {
    std::vector<core::Position> positions = player.secondaryPositions;
    std::ranges::stable_partition(positions, [&](core::Position position) {
        return activeGroup == core::positionGroup(position);
    });
    QVariantList codes;
    for (const core::Position position : positions | std::views::take(kShownSecondaryCodes)) {
        codes.append(QVariantMap{
            {QStringLiteral("code"), positionCode(position)},
            {QStringLiteral("highlighted"), activeGroup == core::positionGroup(position)}});
    }
    return codes;
}

int SquadTexts::hiddenSecondaryCount(const core::Player& player) {
    return std::max(0, static_cast<int>(player.secondaryPositions.size()) - kShownSecondaryCodes);
}

QString SquadTexts::positionTooltip(const core::Player& player,
                                    std::optional<core::PositionGroup> activeGroup) {
    QStringList allNames;
    QStringList activeNames;
    for (const core::Position position : player.secondaryPositions) {
        allNames.append(positionName(position));
        if (activeGroup == core::positionGroup(position)) {
            activeNames.append(positionName(position));
        }
    }
    const QString separator = QStringLiteral(", ");
    const std::optional<core::PositionGroup> primaryGroup =
        player.primaryPosition ? std::optional(core::positionGroup(*player.primaryPosition))
                               : std::nullopt;
    if (activeGroup != primaryGroup && !activeNames.isEmpty()) {
        return tr("Secondary position: %1").arg(activeNames.join(separator));
    }
    const QString primary = positionName(player.primaryPosition);
    return allNames.isEmpty() ? primary
                              : tr("%1. Secondary: %2").arg(primary, allNames.join(separator));
}

QVariantList SquadTexts::positionOptions() {
    return optionsOf<core::Position>([](core::Position position) {
        return QStringLiteral("%1 - %2").arg(positionCode(position), positionName(position));
    });
}

QVariantList SquadTexts::optionalPositionOptions() {
    QVariantList options{QVariantMap{{QStringLiteral("value"), QString{}},
                                     {QStringLiteral("text"), tr("— Not given")}}};
    options.append(positionOptions());
    return options;
}

QVariantList SquadTexts::footOptions() { return optionsOf<core::PreferredFoot>(footName); }

QVariantList SquadTexts::availabilityTypeOptions() {
    return optionsOf<core::AvailabilityType>(availabilityTypeName);
}

} // namespace fdss::viewmodels
