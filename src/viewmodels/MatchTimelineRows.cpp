#include "viewmodels/MatchTimelineRows.hpp"

#include "viewmodels/MatchDayTexts.hpp"

#include <algorithm>
#include <tuple>
#include <vector>

namespace fdss::viewmodels {

namespace {

// The order of the match for the timeline: by minute, the added time of a part before the start
// of the next one.
std::tuple<int, int, int> matchOrder(int minute, std::optional<int> addedTime) {
    return {minute, addedTime ? 0 : 1, addedTime.value_or(0)};
}

} // namespace

QString MatchTimelineRows::changeText(const MatchTimelineInput& input, std::size_t index) {
    if (index >= input.changes.size()) {
        return {};
    }
    if (index >= input.changeNames.size()) {
        return QString::fromStdString(input.changes[index].shape.formation);
    }
    const core::ShapeChangeNames& names = input.changeNames[index];
    return MatchDayTexts::tacticChangeText(QString::fromStdString(names.from),
                                           QString::fromStdString(names.to));
}

QList<QVariantList> MatchTimelineRows::build(const MatchTimelineInput& input) {
    struct Entry {
        std::tuple<int, int, int> order;
        int sequence;
        QVariantList row;
    };
    std::vector<Entry> entries;
    const std::vector<core::MatchEventDraft> drafts(input.events.begin(), input.events.end());
    // Counted in replay order from the yellow cards before each red (P13 review).
    const std::vector<bool> secondYellows = core::secondYellowReds(drafts);
    for (std::size_t index = 0; index < input.events.size(); ++index) {
        const core::MatchEvent& event = input.events[index];
        const bool secondYellow = secondYellows[index];
        QString playerText;
        QString relatedText;
        switch (event.type) {
        case core::MatchEventType::Goal:
            playerText = input.playerName(event.playerId);
            if (event.relatedPlayerId) {
                relatedText = tr("Assist: %1").arg(input.playerName(event.relatedPlayerId));
            }
            break;
        case core::MatchEventType::Sub:
            playerText = tr("On: %1").arg(input.playerName(event.playerId));
            relatedText = tr("Off: %1").arg(input.playerName(event.relatedPlayerId));
            break;
        case core::MatchEventType::OpponentGoal:
            playerText = input.opponentName;
            break;
        case core::MatchEventType::Assist:
        case core::MatchEventType::Yellow:
        case core::MatchEventType::Red:
            playerText = input.playerName(event.playerId);
            break;
        }
        entries.push_back(
            {matchOrder(event.minute, event.addedTime), static_cast<int>(entries.size()),
             QVariantList{QStringLiteral("event"), QVariant::fromValue(event.id.value), 0,
                          MatchDayTexts::minuteText(event.minute, event.addedTime),
                          MatchDayTexts::eventKey(event.type, secondYellow),
                          MatchDayTexts::eventIcon(event.type),
                          MatchDayTexts::eventName(event.type, secondYellow), playerText,
                          relatedText, QString{}, 0, QString{}, 0}});
    }
    for (const core::MatchNote& note : input.notes) {
        const QString text = QString::fromStdString(note.text);
        entries.push_back(
            {matchOrder(note.minute, note.addedTime), static_cast<int>(entries.size()),
             QVariantList{QStringLiteral("note"), 0, QVariant::fromValue(note.id.value),
                          MatchDayTexts::minuteText(note.minute, note.addedTime),
                          QStringLiteral("note"), QStringLiteral("note"), MatchDayTexts::noteName(),
                          input.playerName(note.playerId), text,
                          MatchDayTexts::minuteFieldText({note.minute, note.addedTime}),
                          QVariant::fromValue(note.playerId ? note.playerId->value : 0), text, 0}});
    }
    for (std::size_t index = 0; index < input.changes.size(); ++index) {
        const core::MatchTacticChange& change = input.changes[index];
        entries.push_back(
            {matchOrder(change.minute, change.addedTime), static_cast<int>(entries.size()),
             QVariantList{QStringLiteral("tactic"), 0, 0,
                          MatchDayTexts::minuteText(change.minute, change.addedTime),
                          QStringLiteral("tactic"), QStringLiteral("tactic"),
                          MatchDayTexts::tacticChangeName(), changeText(input, index), QString{},
                          QString{}, 0, QString{}, QVariant::fromValue(change.id.value)}});
    }
    std::ranges::stable_sort(entries, [](const Entry& left, const Entry& right) {
        return std::tie(left.order, left.sequence) > std::tie(right.order, right.sequence);
    });
    QList<QVariantList> rows;
    rows.reserve(static_cast<qsizetype>(entries.size()));
    for (Entry& entry : entries) {
        rows.append(std::move(entry.row));
    }
    return rows;
}

} // namespace fdss::viewmodels
