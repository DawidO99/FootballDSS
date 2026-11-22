#include "data/repositories/MetricRepository.hpp"

#include "data/db/Transaction.hpp"
#include "data/repositories/RepositorySupport.hpp"

#include <utility>
#include <variant>

namespace fdss::data {

namespace {

const auto kSelectDefinition =
    QStringLiteral("SELECT id, key, name_pl, name_en, value_type, unit, min, max, aggregation, "
                   "scope, higher_is_better FROM metric_definition ");

// The date of a value: of its session or match when it has one (they can be moved later).
const auto kSelectValue =
    QStringLiteral("SELECT v.id, v.metric_id, v.player_id, v.match_id, v.session_id, "
                   "COALESCE(s.date, m.date, v.date) AS value_date, v.value FROM metric_value AS v "
                   "LEFT JOIN training_session AS s ON s.id = v.session_id "
                   "LEFT JOIN match AS m ON m.id = v.match_id ");

core::MetricDefinition readDefinition(RowReader& row) {
    core::MetricDefinition definition;
    definition.id = row.id<core::MetricDefinitionId>();
    definition.key = row.text();
    definition.namePl = row.text();
    definition.nameEn = row.text();
    definition.valueType = row.enumeration<core::MetricValueType>();
    definition.unit = row.optionalText();
    definition.min = row.optionalReal();
    definition.max = row.optionalReal();
    definition.aggregation = row.enumeration<core::MetricAggregation>();
    definition.scope = row.enumeration<core::MetricScope>();
    definition.higherIsBetter = row.boolean();
    return definition;
}

core::MetricValue readValue(RowReader& row) {
    core::MetricValue value;
    value.id = row.id<core::MetricValueId>();
    value.metricId = row.id<core::MetricDefinitionId>();
    value.playerId = row.id<core::PlayerId>();
    value.matchId = row.optionalId<core::MatchId>();
    value.sessionId = row.optionalId<core::TrainingSessionId>();
    value.date = row.date();
    value.value = row.real();
    return value;
}

// The condition of one entry and its bound value. Compile-time text only (AGENTS section 7).
struct TargetFilter {
    QString condition;
    QVariant value;
};

TargetFilter filterOf(const core::MetricTarget& target) {
    if (const auto* match = std::get_if<core::MatchId>(&target)) {
        return {QStringLiteral("v.match_id = ?"), idValue(*match)};
    }
    if (const auto* session = std::get_if<core::TrainingSessionId>(&target)) {
        return {QStringLiteral("v.session_id = ?"), idValue(*session)};
    }
    return {QStringLiteral("v.match_id IS NULL AND v.session_id IS NULL AND v.date = ?"),
            dateValue(std::get<core::Date>(target))};
}

std::optional<core::MatchId> matchOf(const core::MetricTarget& target) {
    const auto* match = std::get_if<core::MatchId>(&target);
    return match ? std::optional(*match) : std::nullopt;
}

std::optional<core::TrainingSessionId> sessionOf(const core::MetricTarget& target) {
    const auto* session = std::get_if<core::TrainingSessionId>(&target);
    return session ? std::optional(*session) : std::nullopt;
}

QVariantList definitionValues(const core::MetricDefinitionDraft& draft) {
    return {
        nameValue(draft.namePl),       nameValue(draft.nameEn),      enumValue(draft.valueType),
        optionalTextValue(draft.unit), optionalRealValue(draft.min), optionalRealValue(draft.max),
        enumValue(draft.aggregation),  enumValue(draft.scope),       draft.higherIsBetter ? 1 : 0};
}

} // namespace

core::Result<std::vector<core::MetricDefinition>> MetricRepository::listDefinitions() const {
    return selectAll(m_db, kSelectDefinition + QStringLiteral("ORDER BY id"), {},
                     "list metric definitions", readDefinition);
}

core::Result<core::MetricDefinition>
MetricRepository::getDefinition(core::MetricDefinitionId id) const {
    return selectOne(m_db, kSelectDefinition + QStringLiteral("WHERE id = ?"), {idValue(id)},
                     "get metric definition", readDefinition);
}

core::Result<bool> MetricRepository::keyExists(std::string_view key) const {
    const auto found = selectAll(
        m_db, QStringLiteral("SELECT 1 FROM metric_definition WHERE key = ?"), {textValue(key)},
        "find metric key", [](RowReader& row) { return row.integer(); });
    if (!found) {
        return std::unexpected(found.error());
    }
    return !found->empty();
}

core::Result<core::MetricDefinitionId>
MetricRepository::createDefinition(const core::MetricDefinitionDraft& draft, std::string_view key) {
    QVariantList values{textValue(key)};
    values.append(definitionValues(draft));
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO metric_definition (key, name_pl, name_en, value_type, unit, "
                       "min, max, aggregation, scope, higher_is_better) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
        values, "create metric definition");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::MetricDefinitionId{*id};
}

core::Status MetricRepository::updateDefinition(core::MetricDefinitionId id,
                                                const core::MetricDefinitionDraft& draft) {
    QVariantList values = definitionValues(draft);
    values.append(idValue(id));
    return changeRow(m_db,
                     QStringLiteral("UPDATE metric_definition SET name_pl = ?, name_en = ?, "
                                    "value_type = ?, unit = ?, min = ?, max = ?, aggregation = ?, "
                                    "scope = ?, higher_is_better = ? WHERE id = ?"),
                     values, "update metric definition");
}

core::Status MetricRepository::removeDefinition(core::MetricDefinitionId id) {
    return changeRow(m_db, QStringLiteral("DELETE FROM metric_definition WHERE id = ?"),
                     {idValue(id)}, "delete metric definition", ForeignKeyFailure::RowInUse);
}

core::Result<int> MetricRepository::valueCount(core::MetricDefinitionId id) const {
    return selectOne(m_db, QStringLiteral("SELECT count(*) FROM metric_value WHERE metric_id = ?"),
                     {idValue(id)}, "count metric values",
                     [](RowReader& row) { return row.integer(); });
}

core::Result<std::vector<core::MetricValue>>
MetricRepository::listValues(core::MetricDefinitionId id, const core::MetricTarget& target) const {
    const TargetFilter filter = filterOf(target);
    return selectAll(m_db,
                     kSelectValue + QStringLiteral("WHERE v.metric_id = ? AND ") +
                         filter.condition + QStringLiteral(" ORDER BY v.player_id, v.id"),
                     {idValue(id), filter.value}, "list metric values", readValue);
}

core::Status MetricRepository::saveValues(core::MetricDefinitionId id,
                                          const core::MetricTarget& target, core::Date date,
                                          std::span<const core::MetricValueDraft> values) {
    constexpr std::string_view kContext = "save metric values";
    auto transaction = Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (auto exists =
            requireRow(m_db, QStringLiteral("SELECT 1 FROM metric_definition WHERE id = ?"),
                       {idValue(id)}, kContext);
        !exists) {
        return exists;
    }
    const TargetFilter filter = filterOf(target);
    for (const core::MetricValueDraft& draft : values) {
        const auto stored = selectAll(
            m_db,
            QStringLiteral("SELECT v.value, v.import_id FROM metric_value AS v "
                           "WHERE v.metric_id = ? AND v.player_id = ? AND ") +
                filter.condition,
            {idValue(id), idValue(draft.playerId), filter.value}, kContext, [](RowReader& row) {
                const double value = row.real();
                return std::pair{value, row.optionalId<core::ImportId>()};
            });
        if (!stored) {
            return std::unexpected(stored.error());
        }
        if (draft.value && stored->size() == 1 && stored->front().first == *draft.value) {
            continue;
        }
        // A corrected imported value still goes with the undo of its import (P17 decision 14).
        std::optional<core::ImportId> importId;
        for (const auto& row : *stored) {
            if (!importId) {
                importId = row.second;
            }
        }
        if (!draft.value && stored->empty()) {
            continue;
        }
        if (const auto deleted =
                runSql(m_db,
                       QStringLiteral("DELETE FROM metric_value AS v "
                                      "WHERE v.metric_id = ? AND v.player_id = ? AND ") +
                           filter.condition,
                       {idValue(id), idValue(draft.playerId), filter.value}, kContext);
            !deleted) {
            return std::unexpected(deleted.error());
        }
        if (!draft.value) {
            continue;
        }
        if (auto inserted = insertValue(id, target, date, draft.playerId, *draft.value, importId);
            !inserted) {
            return inserted;
        }
    }
    return transaction->commit();
}

core::Result<std::vector<core::MetricValue>>
MetricRepository::listPlayerValues(core::PlayerId player) const {
    return selectAll(
        m_db, kSelectValue + QStringLiteral("WHERE v.player_id = ? ORDER BY value_date, v.id"),
        {idValue(player)}, "list metric values of a player", readValue);
}

core::Result<std::optional<core::MetricValue>>
MetricRepository::findValue(core::MetricDefinitionId id, const core::MetricTarget& target,
                            core::PlayerId player) const {
    const TargetFilter filter = filterOf(target);
    auto rows =
        selectAll(m_db,
                  kSelectValue + QStringLiteral("WHERE v.metric_id = ? AND v.player_id = ? AND ") +
                      filter.condition + QStringLiteral(" ORDER BY v.id"),
                  {idValue(id), idValue(player), filter.value}, "find metric value", readValue);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    if (rows->empty()) {
        return std::optional<core::MetricValue>{};
    }
    return std::optional(rows->front());
}

core::Status MetricRepository::insertValue(core::MetricDefinitionId id,
                                           const core::MetricTarget& target, core::Date date,
                                           core::PlayerId player, double value,
                                           std::optional<core::ImportId> importId) {
    const auto inserted = insertRow(
        m_db,
        QStringLiteral("INSERT INTO metric_value (metric_id, player_id, match_id, "
                       "session_id, date, value, import_id) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?)"),
        {idValue(id), idValue(player), optionalIdValue(matchOf(target)),
         optionalIdValue(sessionOf(target)), dateValue(date), value, optionalIdValue(importId)},
        "insert metric value");
    if (!inserted) {
        return std::unexpected(inserted.error());
    }
    return {};
}

core::Status MetricRepository::updateValue(core::MetricValueId id, double value) {
    return changeRow(m_db, QStringLiteral("UPDATE metric_value SET value = ? WHERE id = ?"),
                     {value, idValue(id)}, "update metric value");
}

core::Result<int> MetricRepository::countAddedLater(core::ImportId importId) const {
    return selectOne(m_db,
                     QStringLiteral("SELECT COUNT(*) FROM metric_value AS v "
                                    "LEFT JOIN training_session AS s ON s.id = v.session_id "
                                    "LEFT JOIN match AS m ON m.id = v.match_id "
                                    "WHERE (s.import_id = ? OR m.import_id = ?) "
                                    "AND (v.import_id IS NULL OR v.import_id <> ?)"),
                     {idValue(importId), idValue(importId), idValue(importId)},
                     "count metric values added later",
                     [](RowReader& row) { return row.integer(); });
}

core::Result<int> MetricRepository::removeByImport(core::ImportId importId) {
    auto query = runSql(
        m_db,
        QStringLiteral("DELETE FROM metric_value WHERE import_id = ? "
                       "OR session_id IN (SELECT id FROM training_session WHERE import_id = ?) "
                       "OR match_id IN (SELECT id FROM match WHERE import_id = ?)"),
        {idValue(importId), idValue(importId), idValue(importId)},
        "delete metric values of an import");
    if (!query) {
        return std::unexpected(query.error());
    }
    return query->numRowsAffected();
}

} // namespace fdss::data
