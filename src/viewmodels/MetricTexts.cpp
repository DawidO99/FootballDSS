#include "viewmodels/MetricTexts.hpp"

#include "viewmodels/QmlValues.hpp"

#include <QLocale>
#include <QVariantMap>

#include <cmath>

namespace fdss::viewmodels {

namespace {

QString fromUtf8(const std::string& text) { return QString::fromStdString(text).trimmed(); }

} // namespace

QString MetricTexts::name(const core::MetricDefinitionDraft& metric) {
    //: Language code of this translation: "pl" in Polish. It picks the metric name to show.
    const bool polish = tr("en") == QLatin1StringView("pl");
    const QString preferred = fromUtf8(polish ? metric.namePl : metric.nameEn);
    return preferred.isEmpty() ? fromUtf8(polish ? metric.nameEn : metric.namePl) : preferred;
}

QString MetricTexts::pickerText(const core::MetricDefinitionDraft& metric) {
    const QString unit = metric.unit ? fromUtf8(*metric.unit) : QString{};
    //: Metric picker: the name and the unit, e.g. "Sprint 30 m [s]".
    return unit.isEmpty() ? name(metric) : tr("%1 [%2]").arg(name(metric), unit);
}

QString MetricTexts::valueTypeName(core::MetricValueType type) {
    switch (type) {
    case core::MetricValueType::Integer:
        return tr("Whole number");
    case core::MetricValueType::Decimal:
        return tr("Decimal number");
    case core::MetricValueType::Boolean:
        return tr("Yes / no");
    }
    return {};
}

QString MetricTexts::aggregationName(core::MetricAggregation aggregation) {
    switch (aggregation) {
    case core::MetricAggregation::Sum:
        return tr("Sum");
    case core::MetricAggregation::Avg:
        return tr("Average");
    case core::MetricAggregation::Min:
        return tr("Lowest");
    case core::MetricAggregation::Max:
        return tr("Highest");
    case core::MetricAggregation::Last:
        return tr("Last value");
    }
    return {};
}

QString MetricTexts::scopeName(core::MetricScope scope) {
    switch (scope) {
    case core::MetricScope::Match:
        return tr("Match");
    case core::MetricScope::Training:
        return tr("Training session");
    case core::MetricScope::Player:
        return tr("Any day (test)");
    }
    return {};
}

QVariantList MetricTexts::valueTypeOptions() {
    return optionsOf<core::MetricValueType>(&MetricTexts::valueTypeName);
}

QVariantList MetricTexts::scopeOptions() {
    return optionsOf<core::MetricScope>(&MetricTexts::scopeName);
}

QVariantList MetricTexts::aggregationOptions(core::MetricValueType type) {
    QVariantList options;
    for (const core::MetricAggregation aggregation : core::allValues<core::MetricAggregation>()) {
        if (core::isAllowed(type, aggregation)) {
            options.append(QVariantMap{
                {QStringLiteral("value"), QString::fromUtf8(core::toDbText(aggregation))},
                {QStringLiteral("text"), aggregationName(aggregation)}});
        }
    }
    return options;
}

QString MetricTexts::number(double value, int maxDecimals) {
    QString text = QString::number(
        value, 'f', maxDecimals == kShortest ? QLocale::FloatingPointShortest : maxDecimals);
    if (text.contains(u'.')) {
        while (text.endsWith(u'0')) {
            text.chop(1);
        }
        if (text.endsWith(u'.')) {
            text.chop(1);
        }
    }
    if (text == QLatin1StringView("-0")) {
        text = QStringLiteral("0");
    }
    //: Decimal separator of the app language, used in custom metric values.
    return text.replace(u'.', tr("."));
}

QString MetricTexts::value(const core::MetricDefinitionDraft& metric, double value) {
    if (metric.valueType == core::MetricValueType::Boolean) {
        return value != 0.0 ? tr("Yes") : tr("No");
    }
    return number(value);
}

QString MetricTexts::editText(const core::MetricDefinitionDraft& metric, double value) {
    if (metric.valueType == core::MetricValueType::Boolean) {
        return value != 0.0 ? QStringLiteral("1") : QStringLiteral("0");
    }
    return number(value);
}

QString MetricTexts::withUnit(const QString& text, const std::optional<std::string>& unit) {
    const QString unitText = unit ? fromUtf8(*unit) : QString{};
    //: A metric value and its unit, e.g. "4,35 s".
    return unitText.isEmpty() ? text : tr("%1 %2").arg(text, unitText);
}

QString MetricTexts::window(const core::MetricDefinitionDraft& metric, double value, int count) {
    QString shown;
    if (metric.valueType == core::MetricValueType::Boolean &&
        metric.aggregation == core::MetricAggregation::Avg) {
        //: Share of "yes" answers of a yes/no metric.
        shown = tr("%1 %").arg(number(value * 100.0, 0));
    } else if (metric.valueType == core::MetricValueType::Boolean &&
               metric.aggregation == core::MetricAggregation::Sum) {
        //: Count of "yes" answers of a yes/no metric.
        shown = tr("%n time(s) yes", nullptr, static_cast<int>(std::lround(value)));
    } else if (metric.valueType == core::MetricValueType::Boolean) {
        shown = MetricTexts::value(metric, value);
    } else {
        shown = withUnit(number(value, kAggregateDecimals), metric.unit);
    }
    //: Player card: the aggregation of a metric over 28 days, e.g. "Average of 28 days: 4,41 s
    //: (3 values)". %1 the aggregation, %2 the days, %3 the value, %4 the count of values.
    return tr("%1 of %2 days: %3 (%4)")
        .arg(aggregationName(metric.aggregation))
        .arg(core::kMetricWindowDays)
        .arg(shown, tr("%n value(s)", nullptr, count));
}

QString MetricTexts::range(const core::MetricDefinitionDraft& metric) {
    if (metric.min && metric.max) {
        //: Allowed range of a metric, e.g. "3–10 s".
        return withUnit(tr("%1–%2").arg(number(*metric.min), number(*metric.max)), metric.unit);
    }
    if (metric.min) {
        return tr("at least %1").arg(withUnit(number(*metric.min), metric.unit));
    }
    if (metric.max) {
        return tr("at most %1").arg(withUnit(number(*metric.max), metric.unit));
    }
    return {};
}

QString MetricTexts::valueError(const core::MetricDefinitionDraft& metric,
                                const std::string& field) {
    if (field == "value_range") {
        return tr("Out of range: expected %1.").arg(range(metric));
    }
    switch (metric.valueType) {
    case core::MetricValueType::Integer:
        return tr("Enter a whole number, such as 12.");
    case core::MetricValueType::Decimal:
        return tr("Enter a number, such as 4,35 or 4.35.");
    case core::MetricValueType::Boolean:
        return tr("Tick for yes, untick for no.");
    }
    return {};
}

bool MetricTexts::parseLimit(const QString& text, std::optional<double>& limit) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        limit.reset();
        return true;
    }
    limit = core::parseMetricValue(trimmed.toStdString(), core::MetricValueType::Decimal);
    return limit.has_value();
}

QString MetricTexts::limitText(const std::optional<double>& limit) {
    return limit ? number(*limit) : QString{};
}

} // namespace fdss::viewmodels
