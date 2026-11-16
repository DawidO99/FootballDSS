#pragma once

#include "core/domain/Ids.hpp"
#include "core/domain/Lineup.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The `lineup` table (SPEC section 6, packet P12): the lineup of a match (`match_id`) or a template
// (`template_name`). `tactic_json` holds the lineup as `.tactic.json` v1 (data/readers/TacticJson).
// A stored text that cannot be read again means a damaged database: Database.

namespace fdss::data {

class DatabaseManager;

struct LineupTemplateRow {
    core::LineupId id;
    std::string name;
    std::string formation;

    friend bool operator==(const LineupTemplateRow&, const LineupTemplateRow&) = default;
};

class LineupRepository {
public:
    explicit LineupRepository(DatabaseManager& db) : m_db(db) {}

    // nullopt: nothing saved for the match.
    [[nodiscard]] core::Result<std::optional<core::Lineup>> findForMatch(core::MatchId match) const;
    // Replaces the lineup of the match and sets match.formation, in one transaction. A match that
    // is not planned: Conflict; none: NotFound.
    [[nodiscard]] core::Status saveForMatch(core::MatchId match, const core::Lineup& lineup);
    // Removes the lineup of the match; nothing saved is not an error. match.formation stays: the
    // match goes with it (CalendarService::deleteMatch).
    [[nodiscard]] core::Status removeForMatch(core::MatchId match);

    // In the order of the name as stored; the service sorts for the user.
    [[nodiscard]] core::Result<std::vector<LineupTemplateRow>> listTemplates() const;
    [[nodiscard]] core::Result<core::Lineup> getTemplate(core::LineupId id) const;
    // The name is stored trimmed. The service checks it and keeps names unique.
    [[nodiscard]] core::Result<core::LineupId> createTemplate(std::string_view name,
                                                              const core::Lineup& lineup);
    [[nodiscard]] core::Status replaceTemplate(core::LineupId id, const core::Lineup& lineup);
    [[nodiscard]] core::Status removeTemplate(core::LineupId id);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
