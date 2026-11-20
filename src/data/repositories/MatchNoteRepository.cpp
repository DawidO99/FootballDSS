#include "data/repositories/MatchNoteRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

namespace fdss::data {

namespace {

const auto kColumns = QStringLiteral("id, match_id, minute, added_time, player_id, text, seq");

core::MatchNote readNote(RowReader& row) {
    core::MatchNote note;
    note.id = row.id<core::MatchNoteId>();
    note.matchId = row.id<core::MatchId>();
    note.minute = row.integer();
    note.addedTime = row.optionalInteger();
    note.playerId = row.optionalId<core::PlayerId>();
    note.text = row.text();
    note.seq = row.optionalInteger();
    return note;
}

} // namespace

core::Result<core::MatchNoteId> MatchNoteRepository::add(core::MatchId match,
                                                         const core::MatchNoteDraft& draft,
                                                         std::optional<int> seq) {
    const auto id =
        insertRow(m_db,
                  QStringLiteral("INSERT INTO match_note (match_id, minute, added_time, "
                                 "player_id, text, seq) VALUES (?, ?, ?, ?, ?, ?)"),
                  {idValue(match), draft.minute, optionalValue(draft.addedTime),
                   optionalIdValue(draft.playerId), textValue(draft.text), optionalValue(seq)},
                  "add match note");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::MatchNoteId{*id};
}

core::Status MatchNoteRepository::update(core::MatchId match, core::MatchNoteId note,
                                         const core::MatchNoteDraft& draft) {
    return changeRow(m_db,
                     QStringLiteral("UPDATE match_note SET minute = ?, added_time = ?, "
                                    "player_id = ?, text = ? WHERE id = ? AND match_id = ?"),
                     {draft.minute, optionalValue(draft.addedTime), optionalIdValue(draft.playerId),
                      textValue(draft.text), idValue(note), idValue(match)},
                     "update match note");
}

core::Result<core::MatchNote> MatchNoteRepository::get(core::MatchId match,
                                                       core::MatchNoteId note) const {
    return selectOne(m_db,
                     QStringLiteral("SELECT ") + kColumns +
                         QStringLiteral(" FROM match_note WHERE id = ? AND match_id = ?"),
                     {idValue(note), idValue(match)}, "get match note", readNote);
}

core::Result<core::MatchNote> MatchNoteRepository::remove(core::MatchId match,
                                                          core::MatchNoteId note) {
    return selectOne(m_db,
                     QStringLiteral("DELETE FROM match_note WHERE id = ? AND match_id = ? "
                                    "RETURNING ") +
                         kColumns,
                     {idValue(note), idValue(match)}, "remove match note", readNote);
}

core::Result<std::vector<core::MatchNote>> MatchNoteRepository::list(core::MatchId match) const {
    return selectAll(m_db,
                     QStringLiteral("SELECT ") + kColumns +
                         QStringLiteral(" FROM match_note WHERE match_id = ? "
                                        "ORDER BY minute, added_time IS NULL, added_time, id"),
                     {idValue(match)}, "list match notes", readNote);
}

core::Result<std::vector<core::MatchNote>> MatchNoteRepository::deleteWithSeq(core::MatchId match,
                                                                              int seq) {
    return selectAll(m_db,
                     QStringLiteral("DELETE FROM match_note WHERE match_id = ? AND seq = ? "
                                    "RETURNING ") +
                         kColumns,
                     {idValue(match), seq}, "delete match notes of an action", readNote);
}

} // namespace fdss::data
