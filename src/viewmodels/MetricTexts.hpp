#pragma once

#include "core/domain/Metric.hpp"

#include <QCoreApplication>
#include <QString>
#include <QVariantList>

#include <optional>
#include <string>

namespace fdss::viewmodels {

// What the metric screens show of core/domain/Metric (packet P17). Every text follows the language
// on the next call, so a viewmodel asks again on QEvent::LanguageChange.
class MetricTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::MetricTexts)

public:
    // The name in the app language, or the other one when that is empty (P17 decision 9).
    [[nodiscard]] static QString name(const core::MetricDefinitionDraft& metric);
    // The name and the unit for a picker, e.g. "Sprint 30 m [s]".
    [[nodiscard]] static QString pickerText(const core::MetricDefinitionDraft& metric);
    [[nodiscard]] static QString valueTypeName(core::MetricValueType type);
    [[nodiscard]] static QString aggregationName(core::MetricAggregation aggregation);
    [[nodiscard]] static QString scopeName(core::MetricScope scope);

    // [{ value, text }] for the SelectFields of the definition dialog.
    [[nodiscard]] static QVariantList valueTypeOptions();
    [[nodiscard]] static QVariantList scopeOptions();
    // Only those core::isAllowed() for the type.
    [[nodiscard]] static QVariantList aggregationOptions(core::MetricValueType type);

    // With the decimal separator of the app language and at most `maxDecimals` decimals, without
    // trailing zeros: 4,35 and 12.
    [[nodiscard]] static QString number(double value, int maxDecimals = kValueDecimals);
    // One stored value, without the unit: a number, or Yes / No.
    [[nodiscard]] static QString value(const core::MetricDefinitionDraft& metric, double value);
    // What the entry cell holds for a stored value: the number as it would be typed, "1" / "0".
    [[nodiscard]] static QString editText(const core::MetricDefinitionDraft& metric, double value);
    // "4,35 s"; the text alone without a unit.
    [[nodiscard]] static QString withUnit(const QString& text,
                                          const std::optional<std::string>& unit);
    // The aggregation over the window, e.g. "Average of 28 days: 4,41 s (3 values)".
    [[nodiscard]] static QString window(const core::MetricDefinitionDraft& metric, double value,
                                        int count);
    // "3–10 s", "at least 3 s", "at most 10 s"; empty without limits.
    [[nodiscard]] static QString range(const core::MetricDefinitionDraft& metric);
    // What is wrong with a cell, for its row: the text does not parse, or validateValue() failed.
    [[nodiscard]] static QString valueError(const core::MetricDefinitionDraft& metric,
                                            const std::string& field);
    // A limit of the definition dialog as typed: empty is no limit, otherwise a decimal number.
    // False when the text is not a number.
    [[nodiscard]] static bool parseLimit(const QString& text, std::optional<double>& limit);
    // The limit as the dialog shows it; empty for no limit.
    [[nodiscard]] static QString limitText(const std::optional<double>& limit);

    static constexpr int kValueDecimals = 6;
    static constexpr int kAggregateDecimals = 2;
};

} // namespace fdss::viewmodels
