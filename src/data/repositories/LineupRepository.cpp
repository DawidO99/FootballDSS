#include "data/repositories/LineupRepository.hpp"

#include "data/db/Transaction.hpp"
#include "data/readers/TacticJson.hpp"
#include "data/repositories/RepositorySupport.hpp"

#include <format>
#include <string_view>

namespace fdss::data {

namespace {

// The stored text, read again. Not readable: the database is damaged.
core::Result<core::Lineup> parseStored(const std::string& json, std::string_view context) {
    auto lineup = fromTacticJson(QByteArray::fromStdString(json));
    if (!lineup) {
        return std::unexpected(core::Error{
            core::ErrorCode::Database,
            std::format("{}: stored tactic_json is not valid: {}", context, lineup.error().detail),
            {}});
    }
    return lineup;
}

QVariant storedValue(const core::Lineup& lineup) {
    return QString::fromUtf8(toTacticJson(lineup, QJsonDocument::Compact));
}

} // namespace

core::Result<std::optional<core::Lineup>>
LineupRepository::findForMatch(core::MatchId match) const {
    constexpr std::string_view kContext = "find match lineup";
    auto rows = selectAll(
        m_db, QStringLiteral("SELECT tactic_json FROM lineup WHERE match_id = ? ORDER BY id DESC"),
        {idValue(match)}, kContext, [](RowReader& row) { return row.text(); });
    if (!rows) {
        return std::unexpected(rows.error());
    }
    if (rows->empty()) {
        return std::optional<core::Lineup>{};
    }
    auto lineup = parseStored(rows->front(), kContext);
    if (!lineup) {
        return std::unexpected(lineup.error());
    }
    return std::optional<core::Lineup>(std::move(*lineup));
}

core::Status LineupRepository::saveForMatch(core::MatchId match, const core::Lineup& lineup) {
    constexpr std::string_view kContext = "save match lineup";
    auto transaction = Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    auto status =
        selectOne(m_db, QStringLiteral("SELECT status FROM match WHERE id = ?"), {idValue(match)},
                  kContext, [](RowReader& row) { return row.enumeration<core::MatchStatus>(); });
    if (!status) {
        return std::unexpected(status.error());
    }
    if (*status != core::MatchStatus::Planned) {
        return std::unexpected(core::Error{
            core::ErrorCode::Conflict, std::format("{}: the match is not planned", kContext), {}});
    }
    if (auto removed = removeForMatch(match); !removed) {
        return removed;
    }
    const auto inserted =
        insertRow(m_db,
                  QStringLiteral("INSERT INTO lineup (match_id, template_name, formation, "
                                 "tactic_json) VALUES (?, NULL, ?, ?)"),
                  {idValue(match), textValue(lineup.formation), storedValue(lineup)}, kContext);
    if (!inserted) {
        return std::unexpected(inserted.error());
    }
    if (auto updated =
            changeRow(m_db, QStringLiteral("UPDATE match SET formation = ? WHERE id = ?"),
                      {textValue(lineup.formation), idValue(match)}, kContext);
        !updated) {
        return updated;
    }
    return transaction->commit();
}

core::Status LineupRepository::removeForMatch(core::MatchId match) {
    const auto deleted = runSql(m_db, QStringLiteral("DELETE FROM lineup WHERE match_id = ?"),
                                {idValue(match)}, "remove match lineup");
    if (!deleted) {
        return std::unexpected(deleted.error());
    }
    return {};
}

core::Result<std::vector<LineupTemplateRow>> LineupRepository::listTemplates() const {
    return selectAll(m_db,
                     QStringLiteral("SELECT id, template_name, formation FROM lineup "
                                    "WHERE template_name IS NOT NULL ORDER BY template_name, id"),
                     {}, "list lineup templates", [](RowReader& row) {
                         LineupTemplateRow result;
                         result.id = row.id<core::LineupId>();
                         result.name = row.text();
                         result.formation = row.text();
                         return result;
                     });
}

core::Result<core::Lineup> LineupRepository::getTemplate(core::LineupId id) const {
    constexpr std::string_view kContext = "get lineup template";
    auto json = selectOne(m_db,
                          QStringLiteral("SELECT tactic_json FROM lineup "
                                         "WHERE id = ? AND template_name IS NOT NULL"),
                          {idValue(id)}, kContext, [](RowReader& row) { return row.text(); });
    if (!json) {
        return std::unexpected(json.error());
    }
    return parseStored(*json, kContext);
}

core::Result<core::LineupId> LineupRepository::createTemplate(std::string_view name,
                                                              const core::Lineup& lineup) {
    const auto id = insertRow(m_db,
                              QStringLiteral("INSERT INTO lineup (match_id, template_name, "
                                             "formation, tactic_json) VALUES (NULL, ?, ?, ?)"),
                              {nameValue(name), textValue(lineup.formation), storedValue(lineup)},
                              "create lineup template");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::LineupId{*id};
}

core::Status LineupRepository::replaceTemplate(core::LineupId id, const core::Lineup& lineup) {
    return changeRow(m_db,
                     QStringLiteral("UPDATE lineup SET formation = ?, tactic_json = ? "
                                    "WHERE id = ? AND template_name IS NOT NULL"),
                     {textValue(lineup.formation), storedValue(lineup), idValue(id)},
                     "replace lineup template");
}

core::Status LineupRepository::removeTemplate(core::LineupId id) {
    return changeRow(
        m_db, QStringLiteral("DELETE FROM lineup WHERE id = ? AND template_name IS NOT NULL"),
        {idValue(id)}, "remove lineup template");
}

} // namespace fdss::data
