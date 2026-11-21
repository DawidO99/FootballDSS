#pragma once

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/MatchEvent.hpp"
#include "core/domain/MatchNote.hpp"
#include "core/domain/MatchTacticChange.hpp"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QVariantList>

#include <functional>
#include <optional>
#include <span>

namespace fdss::viewmodels {

// What the timeline shows of a match.
struct MatchTimelineInput {
    // In the order of the match.
    std::span<const core::MatchEvent> events;
    std::span<const core::MatchNote> notes;
    // In the order of entry, with the names before and after each (core::shapeChangeNames());
    // without names a change shows its stored formation.
    std::span<const core::MatchTacticChange> changes;
    std::span<const core::ShapeChangeNames> changeNames;
    std::function<QString(std::optional<core::PlayerId>)> playerName;
    QString opponentName;
};

// The rows of MatchTimelineModel (Match Day, packet P13, and the match report, packet P14): events,
// notes and changes of shape, the latest in the match first.
class MatchTimelineRows {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::MatchTimelineRows)

public:
    [[nodiscard]] static QList<QVariantList> build(const MatchTimelineInput& input);
    // "4-4-2 → 4-2-3-1" for the change at `index` of the input.
    [[nodiscard]] static QString changeText(const MatchTimelineInput& input, std::size_t index);
};

} // namespace fdss::viewmodels
