#include "data/repositories/MatchTacticRepository.hpp"

#include "data/readers/TacticJson.hpp"
#include "data/repositories/RepositorySupport.hpp"

#include <format>
#include <string>
#include <string_view>

namespace fdss::data {

namespace {

const auto kColumns =
    QStringLiteral("id, match_id, minute, added_time, formation, tactic_json, seq");

struct StoredChange {
    core::MatchTacticChange change;
    std::string json;
};

StoredChange readRow(RowReader& row) {
    StoredChange stored;
    stored.change.id = row.id<core::MatchTacticChangeId>();
    stored.change.matchId = row.id<core::MatchId>();
    stored.change.minute = row.integer();
    stored.change.addedTime = row.optionalInteger();
    stored.change.shape.formation = row.text();
    stored.json = row.text();
    stored.change.seq = row.integer();
    return stored;
}

// The stored text, read again. Not readable: the database is damaged.
core::Result<core::MatchTacticChange> parseStored(StoredChange stored, std::string_view context) {
    auto shape = fromTacticJson(QByteArray::fromStdString(stored.json));
    if (!shape) {
        return std::unexpected(core::Error{
            core::ErrorCode::Database,
            std::format("{}: stored tactic_json is not valid: {}", context, shape.error().detail),
            {}});
    }
    // The name of the column: the occupied slots at the change, not the 11 slots of the file.
    shape->formation = std::move(stored.change.shape.formation);
    stored.change.shape = std::move(*shape);
    return std::move(stored.change);
}

// The places only: no players, no bench.
QVariant storedValue(const core::Lineup& shape) {
    core::Lineup places = shape;
    places.bench.clear();
    for (core::LineupSlot& slot : places.pitchSlots) {
        slot.player.reset();
    }
    return QString::fromUtf8(toTacticJson(places, QJsonDocument::Compact));
}

} // namespace

core::Result<std::vector<core::MatchTacticChange>>
MatchTacticRepository::list(core::MatchId match) const {
    constexpr std::string_view kContext = "list tactic changes";
    auto rows = selectAll(m_db,
                          QStringLiteral("SELECT ") + kColumns +
                              QStringLiteral(" FROM match_tactic_change WHERE match_id = ? "
                                             "ORDER BY seq"),
                          {idValue(match)}, kContext, readRow);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    std::vector<core::MatchTacticChange> changes;
    changes.reserve(rows->size());
    for (StoredChange& row : *rows) {
        auto change = parseStored(std::move(row), kContext);
        if (!change) {
            return std::unexpected(change.error());
        }
        changes.push_back(std::move(*change));
    }
    return changes;
}

core::Result<core::MatchTacticChangeId>
MatchTacticRepository::create(core::MatchId match, const core::MatchTacticChangeDraft& change,
                              int seq) {
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO match_tactic_change (match_id, minute, added_time, formation, "
                       "tactic_json, seq) VALUES (?, ?, ?, ?, ?, ?)"),
        {idValue(match), change.minute, optionalValue(change.addedTime),
         textValue(change.shape.formation), storedValue(change.shape), seq},
        "add tactic change");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::MatchTacticChangeId{*id};
}

core::Result<std::optional<core::MatchTacticChange>>
MatchTacticRepository::deleteWithSeq(core::MatchId match, int seq) {
    constexpr std::string_view kContext = "delete tactic change of an action";
    auto rows = selectAll(m_db,
                          QStringLiteral("DELETE FROM match_tactic_change WHERE match_id = ? AND "
                                         "seq = ? RETURNING ") +
                              kColumns,
                          {idValue(match), seq}, kContext, readRow);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    if (rows->empty()) {
        return std::optional<core::MatchTacticChange>{};
    }
    auto change = parseStored(std::move(rows->front()), kContext);
    if (!change) {
        return std::unexpected(change.error());
    }
    return std::optional<core::MatchTacticChange>(std::move(*change));
}

} // namespace fdss::data
