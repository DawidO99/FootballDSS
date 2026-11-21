#include "data/repositories/MatchRepository.hpp"

#include "data/db/Transaction.hpp"
#include "data/repositories/RepositorySupport.hpp"

#include <format>
#include <string_view>

namespace fdss::data {

namespace {

const auto kSelectMatch =
    QStringLiteral("SELECT id, team_id, opponent_id, date, kickoff_time, venue, competition, "
                   "goals_for, goals_against, status, formation FROM match ");
const auto kMatchExists = QStringLiteral("SELECT 1 FROM match WHERE id = ?");
const auto kEventColumns =
    QStringLiteral("id, match_id, minute, type, player_id, related_player_id, added_time, seq");

core::Match readMatch(RowReader& row) {
    core::Match match;
    match.id = row.id<core::MatchId>();
    match.teamId = row.id<core::TeamId>();
    match.opponentId = row.id<core::OpponentId>();
    match.date = row.date();
    match.kickoffTime = row.optionalTime();
    match.venue = row.enumeration<core::Venue>();
    match.competition = row.optionalText();
    match.goalsFor = row.optionalInteger();
    match.goalsAgainst = row.optionalInteger();
    match.status = row.enumeration<core::MatchStatus>();
    match.formation = row.optionalText();
    return match;
}

core::Appearance readAppearance(RowReader& row) {
    core::Appearance appearance;
    appearance.id = row.id<core::AppearanceId>();
    appearance.matchId = row.id<core::MatchId>();
    appearance.playerId = row.id<core::PlayerId>();
    appearance.started = row.boolean();
    appearance.minuteOn = row.optionalInteger();
    appearance.minuteOff = row.optionalInteger();
    appearance.position = row.optionalEnumeration<core::Position>();
    appearance.rating = row.optionalInteger();
    appearance.rpe = row.optionalInteger();
    return appearance;
}

core::MatchEvent readEvent(RowReader& row) {
    core::MatchEvent event;
    event.id = row.id<core::MatchEventId>();
    event.matchId = row.id<core::MatchId>();
    event.minute = row.integer();
    event.type = row.enumeration<core::MatchEventType>();
    event.playerId = row.optionalId<core::PlayerId>();
    event.relatedPlayerId = row.optionalId<core::PlayerId>();
    event.addedTime = row.optionalInteger();
    event.seq = row.optionalInteger();
    return event;
}

// A statement limited to planned matches changed nothing: either there is no such match, or it is
// not planned any more.
core::Status explainNoPlannedMatch(DatabaseManager& db, core::MatchId id,
                                   std::string_view context) {
    if (auto exists = requireRow(db, kMatchExists, {idValue(id)}, context); !exists) {
        return exists;
    }
    return std::unexpected(core::Error{
        core::ErrorCode::Conflict, std::format("{}: the match is not planned", context), {}});
}

} // namespace

core::Result<std::vector<core::Match>> MatchRepository::list(core::TeamId team,
                                                             core::DateRange range) const {
    return selectAll(m_db,
                     kSelectMatch + QStringLiteral("WHERE team_id = ? "
                                                   "AND date >= COALESCE(?, '0000-01-01') "
                                                   "AND date <= COALESCE(?, '9999-12-31') "
                                                   "ORDER BY date, kickoff_time, id"),
                     {idValue(team), optionalDateValue(range.from), optionalDateValue(range.to)},
                     "list matches", readMatch);
}

core::Result<core::Match> MatchRepository::get(core::MatchId id) const {
    return selectOne(m_db, kSelectMatch + QStringLiteral("WHERE id = ?"), {idValue(id)},
                     "get match", readMatch);
}

core::Result<core::MatchId> MatchRepository::create(core::TeamId team,
                                                    const core::MatchDraft& draft) {
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO match (team_id, opponent_id, date, kickoff_time, venue, "
                       "competition) VALUES (?, ?, ?, ?, ?, ?)"),
        {idValue(team), idValue(draft.opponentId), dateValue(draft.date),
         optionalTimeValue(draft.kickoffTime), enumValue(draft.venue),
         optionalTextValue(draft.competition)},
        "create match");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::MatchId{*id};
}

core::Status MatchRepository::updatePlanned(core::MatchId id, const core::MatchDraft& draft) {
    constexpr std::string_view kContext = "update planned match";
    auto status = changeRow(
        m_db,
        QStringLiteral("UPDATE match SET opponent_id = ?, date = ?, kickoff_time = ?, venue = ?, "
                       "competition = ? WHERE id = ? AND status = ?"),
        {idValue(draft.opponentId), dateValue(draft.date), optionalTimeValue(draft.kickoffTime),
         enumValue(draft.venue), optionalTextValue(draft.competition), idValue(id),
         enumValue(core::MatchStatus::Planned)},
        kContext);
    if (!status && status.error().code == core::ErrorCode::NotFound) {
        return explainNoPlannedMatch(m_db, id, kContext);
    }
    return status;
}

core::Status MatchRepository::removePlanned(core::MatchId id) {
    constexpr std::string_view kContext = "delete planned match";
    auto status = changeRow(m_db, QStringLiteral("DELETE FROM match WHERE id = ? AND status = ?"),
                            {idValue(id), enumValue(core::MatchStatus::Planned)}, kContext,
                            ForeignKeyFailure::RowInUse);
    if (!status && status.error().code == core::ErrorCode::NotFound) {
        return explainNoPlannedMatch(m_db, id, kContext);
    }
    return status;
}

core::Status MatchRepository::setResult(core::MatchId id, std::optional<int> goalsFor,
                                        std::optional<int> goalsAgainst, core::MatchStatus status) {
    return changeRow(
        m_db,
        QStringLiteral("UPDATE match SET goals_for = ?, goals_against = ?, status = ? "
                       "WHERE id = ?"),
        {optionalValue(goalsFor), optionalValue(goalsAgainst), enumValue(status), idValue(id)},
        "set match result");
}

core::Status MatchRepository::setStatus(core::MatchId id, core::MatchStatus status) {
    return changeRow(m_db, QStringLiteral("UPDATE match SET status = ? WHERE id = ?"),
                     {enumValue(status), idValue(id)}, "set match status");
}

core::Result<std::optional<core::MatchId>> MatchRepository::findLive(core::TeamId team) const {
    auto ids = selectAll(m_db,
                         QStringLiteral("SELECT id FROM match WHERE team_id = ? AND status = ? "
                                        "ORDER BY date, id LIMIT 1"),
                         {idValue(team), enumValue(core::MatchStatus::Live)}, "find live match",
                         [](RowReader& row) { return row.id<core::MatchId>(); });
    if (!ids) {
        return std::unexpected(ids.error());
    }
    if (ids->empty()) {
        return std::optional<core::MatchId>{};
    }
    return std::optional<core::MatchId>{ids->front()};
}

core::Result<std::vector<std::string>> MatchRepository::listCompetitions(core::TeamId team) const {
    return selectAll(m_db,
                     QStringLiteral("SELECT competition FROM match "
                                    "WHERE team_id = ? AND competition IS NOT NULL "
                                    "GROUP BY competition ORDER BY MAX(date) DESC, competition"),
                     {idValue(team)}, "list competitions",
                     [](RowReader& row) { return row.text(); });
}

core::Status
MatchRepository::replaceAppearances(core::MatchId match,
                                    std::span<const core::AppearanceDraft> appearances) {
    constexpr std::string_view kContext = "replace appearances";
    auto transaction = Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    // Without this an empty list for a missing match would look like a success.
    if (auto exists = requireRow(m_db, kMatchExists, {idValue(match)}, kContext); !exists) {
        return exists;
    }
    if (const auto deleted =
            runSql(m_db, QStringLiteral("DELETE FROM appearance WHERE match_id = ?"),
                   {idValue(match)}, kContext);
        !deleted) {
        return std::unexpected(deleted.error());
    }
    for (const core::AppearanceDraft& appearance : appearances) {
        const auto inserted = insertRow(
            m_db,
            QStringLiteral("INSERT INTO appearance (match_id, player_id, started, minute_on, "
                           "minute_off, position, rating, rpe) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
            {idValue(match), idValue(appearance.playerId), appearance.started ? 1 : 0,
             optionalValue(appearance.minuteOn), optionalValue(appearance.minuteOff),
             optionalEnumValue(appearance.position), optionalValue(appearance.rating),
             optionalValue(appearance.rpe)},
            kContext);
        if (!inserted) {
            return std::unexpected(inserted.error());
        }
    }
    return transaction->commit();
}

core::Result<std::vector<core::Appearance>>
MatchRepository::listAppearances(core::MatchId match) const {
    return selectAll(m_db,
                     QStringLiteral("SELECT id, match_id, player_id, started, minute_on, "
                                    "minute_off, position, rating, rpe FROM appearance "
                                    "WHERE match_id = ? ORDER BY id"),
                     {idValue(match)}, "list appearances", readAppearance);
}

core::Status MatchRepository::setRatings(core::MatchId match,
                                         std::span<const AppearanceRating> ratings) {
    constexpr std::string_view kContext = "set appearance ratings";
    for (const AppearanceRating& rating : ratings) {
        if (auto changed = changeRow(m_db,
                                     QStringLiteral("UPDATE appearance SET rating = ?, rpe = ? "
                                                    "WHERE match_id = ? AND player_id = ?"),
                                     {optionalValue(rating.rating), optionalValue(rating.rpe),
                                      idValue(match), idValue(rating.player)},
                                     kContext);
            !changed) {
            return changed;
        }
    }
    return {};
}

core::Result<std::vector<PlayerAppearanceRow>>
MatchRepository::listPlayerAppearances(core::PlayerId player) const {
    return selectAll(
        m_db,
        QStringLiteral(
            "SELECT a.id, a.match_id, a.player_id, a.started, a.minute_on, a.minute_off, "
            "a.position, a.rating, a.rpe, m.id, m.team_id, m.opponent_id, m.date, "
            "m.kickoff_time, m.venue, m.competition, m.goals_for, m.goals_against, m.status, "
            "m.formation, o.name FROM appearance a JOIN match m ON m.id = a.match_id "
            "JOIN opponent o ON o.id = m.opponent_id WHERE a.player_id = ? AND m.status = ? "
            "ORDER BY m.date DESC, m.kickoff_time IS NULL, m.kickoff_time DESC, m.id DESC"),
        {idValue(player), enumValue(core::MatchStatus::Finished)}, "list player appearances",
        [](RowReader& row) {
            PlayerAppearanceRow result;
            result.appearance = readAppearance(row);
            result.match = readMatch(row);
            result.opponentName = row.text();
            return result;
        });
}

core::Result<std::optional<std::string>> MatchRepository::reportNote(core::MatchId match) const {
    return selectOne(m_db, QStringLiteral("SELECT report_note FROM match WHERE id = ?"),
                     {idValue(match)}, "get report note",
                     [](RowReader& row) { return row.optionalText(); });
}

core::Status MatchRepository::setReportNote(core::MatchId match,
                                            const std::optional<std::string>& note) {
    return changeRow(m_db, QStringLiteral("UPDATE match SET report_note = ? WHERE id = ?"),
                     {optionalTextValue(note), idValue(match)}, "set report note");
}

core::Result<core::MatchEventId> MatchRepository::appendEvent(core::MatchId match,
                                                              const core::MatchEventDraft& draft,
                                                              std::optional<int> seq) {
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO match_event (match_id, minute, type, player_id, "
                       "related_player_id, added_time, seq) VALUES (?, ?, ?, ?, ?, ?, ?)"),
        {idValue(match), draft.minute, enumValue(draft.type), optionalIdValue(draft.playerId),
         optionalIdValue(draft.relatedPlayerId), optionalValue(draft.addedTime),
         optionalValue(seq)},
        "append match event");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::MatchEventId{*id};
}

core::Result<std::vector<core::MatchEvent>> MatchRepository::listEvents(core::MatchId match) const {
    return selectAll(m_db,
                     QStringLiteral("SELECT id, match_id, minute, type, player_id, "
                                    "related_player_id, added_time, seq FROM match_event "
                                    "WHERE match_id = ? "
                                    "ORDER BY minute, added_time IS NULL, added_time, id"),
                     {idValue(match)}, "list match events", readEvent);
}

core::Result<core::MatchEvent> MatchRepository::deleteLastEvent(core::MatchId match) {
    return selectOne(m_db,
                     QStringLiteral("DELETE FROM match_event WHERE id = "
                                    "(SELECT MAX(id) FROM match_event WHERE match_id = ?) "
                                    "RETURNING id, match_id, minute, type, player_id, "
                                    "related_player_id, added_time, seq"),
                     {idValue(match)}, "delete last match event", readEvent);
}

core::Result<core::MatchEvent> MatchRepository::deleteEvent(core::MatchId match,
                                                            core::MatchEventId event) {
    return selectOne(m_db,
                     QStringLiteral("DELETE FROM match_event WHERE match_id = ? AND id = ? "
                                    "RETURNING ") +
                         kEventColumns,
                     {idValue(match), idValue(event)}, "delete match event", readEvent);
}

core::Status MatchRepository::restoreEvent(const core::MatchEvent& event) {
    const auto id =
        insertRow(m_db,
                  QStringLiteral("INSERT INTO match_event (") + kEventColumns +
                      QStringLiteral(") VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
                  {idValue(event.id), idValue(event.matchId), event.minute, enumValue(event.type),
                   optionalIdValue(event.playerId), optionalIdValue(event.relatedPlayerId),
                   optionalValue(event.addedTime), optionalValue(event.seq)},
                  "restore match event");
    if (!id) {
        return std::unexpected(id.error());
    }
    return {};
}

core::Result<std::vector<core::MatchEvent>>
MatchRepository::deleteEventsWithSeq(core::MatchId match, int seq) {
    return selectAll(m_db,
                     QStringLiteral("DELETE FROM match_event WHERE match_id = ? AND seq = ? "
                                    "RETURNING id, match_id, minute, type, player_id, "
                                    "related_player_id, added_time, seq"),
                     {idValue(match), seq}, "delete match events of an action", readEvent);
}

} // namespace fdss::data
