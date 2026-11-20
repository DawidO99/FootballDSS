#pragma once

#include "core/domain/Ids.hpp"
#include "core/domain/MatchNote.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <vector>

namespace fdss::data {

class DatabaseManager;

// The notes of Match Day (`match_note`, packet P13). The service validates the text and the minute;
// the CHECKs of the table are the last guard. Writes join the transaction of the caller.
class MatchNoteRepository {
public:
    explicit MatchNoteRepository(DatabaseManager& db) : m_db(db) {}

    // A missing match or player, or values out of range: InvalidInput.
    [[nodiscard]] core::Result<core::MatchNoteId>
    add(core::MatchId match, const core::MatchNoteDraft& draft, std::optional<int> seq);
    // Minute, player and text; the seq stays. Not a note of that match: NotFound.
    [[nodiscard]] core::Status update(core::MatchId match, core::MatchNoteId note,
                                      const core::MatchNoteDraft& draft);
    // Not a note of that match: NotFound.
    [[nodiscard]] core::Result<core::MatchNote> get(core::MatchId match,
                                                    core::MatchNoteId note) const;
    [[nodiscard]] core::Result<core::MatchNote> remove(core::MatchId match, core::MatchNoteId note);
    // In the order of the match, as the events (core::replayOrder()).
    [[nodiscard]] core::Result<std::vector<core::MatchNote>> list(core::MatchId match) const;
    // Removes the notes of one Match Day action and returns them; none: empty.
    [[nodiscard]] core::Result<std::vector<core::MatchNote>> deleteWithSeq(core::MatchId match,
                                                                           int seq);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
