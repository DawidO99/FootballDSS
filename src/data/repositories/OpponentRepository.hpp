#pragma once

#include "core/domain/Opponent.hpp"
#include "core/util/Error.hpp"

#include <vector>

namespace fdss::data {

class DatabaseManager;

// Names are unique ignoring case, through name_key: QString::toCaseFolded() of the trimmed name,
// because SQLite NOCASE folds ASCII only. A duplicate gives Conflict with field = "name".
class OpponentRepository {
public:
    explicit OpponentRepository(DatabaseManager& db) : m_db(db) {}

    [[nodiscard]] core::Result<std::vector<core::Opponent>> list() const;
    [[nodiscard]] core::Result<core::Opponent> get(core::OpponentId id) const;
    [[nodiscard]] core::Result<core::OpponentId> create(const core::OpponentDraft& draft);
    [[nodiscard]] core::Status update(core::OpponentId id, const core::OpponentDraft& draft);
    // Only an opponent no match refers to (the undo of an import, packet P18 decision 10);
    // otherwise Conflict. Missing: NotFound.
    [[nodiscard]] core::Status remove(core::OpponentId id);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
