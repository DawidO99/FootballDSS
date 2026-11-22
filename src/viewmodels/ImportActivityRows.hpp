#pragma once

#include "core/domain/Metric.hpp"
#include "core/parsing/MatchImport.hpp"
#include "core/parsing/TrainingImport.hpp"
#include "services/import/ImportService.hpp"

#include <QCoreApplication>
#include <QString>
#include <QVariantList>

#include <span>
#include <vector>

// What the import wizard shows for the training and match imports (packet P18): the field
// choices, the issues of a row and the rows of the report. Every text follows the language on
// the next call, so the viewmodel asks again on QEvent::LanguageChange.

namespace fdss::viewmodels {

// The value of a mapping choice that opens the metric dialog instead of mapping the column.
inline constexpr auto kCreateMetricChoice = "create_metric";
// The prefix of a mapping choice that maps the column to a metric: "metric:<key>".
inline constexpr auto kMetricChoicePrefix = "metric:";

class ImportActivityTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::ImportActivityTexts)

public:
    [[nodiscard]] static QString fieldName(services::ImportKind kind, int field);
    // [{ value, text }]: ignore, the fields, "metric:<key>" of every metric, and "Create metric…"
    // when `canCreateMetric`.
    [[nodiscard]] static QVariantList fieldOptions(services::ImportKind kind,
                                                   std::span<const core::MetricDefinition> metrics,
                                                   bool canCreateMetric);
    [[nodiscard]] static QString mappingIssue(core::ActivityMappingIssue issue);
    // One issue, e.g. "RPE '11', expected a whole number from 0 to 10".
    [[nodiscard]] static QString issue(services::ImportKind kind, const core::ActivityIssue& issue,
                                       std::span<const core::MetricDefinition> metrics);
    [[nodiscard]] static QString actionName(core::ActivityAction action);
    // "Not imported: 4 goals, 3 assists, 2 cards — no minute; enter them in the match report."
    // Empty without any.
    [[nodiscard]] static QString notImported(const core::NotImportedEvents& events);
    // The warning about yellow cards and suspensions; empty without yellow cards.
    [[nodiscard]] static QString suspensionNote(const core::NotImportedEvents& events);
    // "Undo also deletes 3 notes and 12 values added after the import. Continue?"
    [[nodiscard]] static QString undoQuestion(const services::ImportUndoPreview& preview);
};

struct ActivityReportRow {
    int rowNumber = 0;
    core::RowStatus status = core::RowStatus::Ok;
    QString name;
    QString action;
    QString details;
    QString message; // "Row 14: …."; empty without issues
};

[[nodiscard]] std::vector<ActivityReportRow>
trainingReport(const core::TrainingImport& import, const core::TrainingPlan& plan,
               std::span<const core::MetricDefinition> metrics);
[[nodiscard]] std::vector<ActivityReportRow>
matchReport(const core::MatchImport& import, const core::MatchPlan& plan,
            std::span<const core::MetricDefinition> metrics);

} // namespace fdss::viewmodels
