#pragma once

#include "core/domain/Ids.hpp"
#include "core/domain/MatchTacticChange.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <vector>

namespace fdss::data {

class DatabaseManager;

// The tactic changes of Match Day (`match_tactic_change`, packet P13b). `tactic_json` holds the
// places of the 11 slots as `.tactic.json` v1 (data/readers/TacticJson) without players; a stored
// text that cannot be read again means a damaged database: Database. The service validates the
// shape and the minute; the CHECKs of the table are the last guard. Writes join the transaction of
// the caller.
class MatchTacticRepository {
public:
    explicit MatchTacticRepository(DatabaseManager& db) : m_db(db) {}

    // In the order of entry (seq). The shape keeps the stored formation name.
    [[nodiscard]] core::Result<std::vector<core::MatchTacticChange>>
    list(core::MatchId match) const;
    // The players of the shape are not stored. A missing match or values out of range:
    // InvalidInput; a seq used twice: Conflict.
    [[nodiscard]] core::Result<core::MatchTacticChangeId>
    create(core::MatchId match, const core::MatchTacticChangeDraft& change, int seq);
    // Removes the change of one Match Day action and returns it; none: nullopt.
    [[nodiscard]] core::Result<std::optional<core::MatchTacticChange>>
    deleteWithSeq(core::MatchId match, int seq);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
