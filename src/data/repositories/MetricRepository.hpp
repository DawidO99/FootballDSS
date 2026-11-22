#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/Metric.hpp"
#include "core/util/Error.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fdss::data {

class DatabaseManager;

// metric_definition and metric_value (packet P17).
class MetricRepository {
public:
    explicit MetricRepository(DatabaseManager& db) : m_db(db) {}

    // In the order they were created.
    [[nodiscard]] core::Result<std::vector<core::MetricDefinition>> listDefinitions() const;
    [[nodiscard]] core::Result<core::MetricDefinition>
    getDefinition(core::MetricDefinitionId id) const;
    [[nodiscard]] core::Result<bool> keyExists(std::string_view key) const;
    // A key that is taken: Conflict with field "key".
    [[nodiscard]] core::Result<core::MetricDefinitionId>
    createDefinition(const core::MetricDefinitionDraft& draft, std::string_view key);
    // The key stays.
    [[nodiscard]] core::Status updateDefinition(core::MetricDefinitionId id,
                                                const core::MetricDefinitionDraft& draft);
    // A definition with values: Conflict.
    [[nodiscard]] core::Status removeDefinition(core::MetricDefinitionId id);
    [[nodiscard]] core::Result<int> valueCount(core::MetricDefinitionId id) const;

    // The values of one entry: of the match, of the session, or of the day without either.
    [[nodiscard]] core::Result<std::vector<core::MetricValue>>
    listValues(core::MetricDefinitionId id, const core::MetricTarget& target) const;
    // One transaction; on any error nothing changes. Per player of `values`: a row with the same
    // single stored value stays as it is (its import_id too); otherwise the stored rows of the
    // player for this entry go and the value, if any, is inserted with `date`. Players not in
    // `values` keep their rows.
    [[nodiscard]] core::Status saveValues(core::MetricDefinitionId id,
                                          const core::MetricTarget& target, core::Date date,
                                          std::span<const core::MetricValueDraft> values);
    // Every value of the player, oldest first (date, then id). A value of a session or a match has
    // the date of that session or match, also when it was moved after the value was entered.
    [[nodiscard]] core::Result<std::vector<core::MetricValue>>
    listPlayerValues(core::PlayerId player) const;

    // --- Import (packet P18). Each joins the transaction of the caller. ---
    [[nodiscard]] core::Result<std::optional<core::MetricValue>>
    findValue(core::MetricDefinitionId id, const core::MetricTarget& target,
              core::PlayerId player) const;
    [[nodiscard]] core::Status insertValue(core::MetricDefinitionId id,
                                           const core::MetricTarget& target, core::Date date,
                                           core::PlayerId player, double value,
                                           std::optional<core::ImportId> importId);
    // Its import_id stays. None: NotFound.
    [[nodiscard]] core::Status updateValue(core::MetricValueId id, double value);
    // Values in the sessions and matches of the import that the import did not write.
    [[nodiscard]] core::Result<int> countAddedLater(core::ImportId importId) const;
    // Deletes the values the import wrote and every value of its sessions and matches.
    [[nodiscard]] core::Result<int> removeByImport(core::ImportId importId);

private:
    DatabaseManager& m_db;
};

} // namespace fdss::data
