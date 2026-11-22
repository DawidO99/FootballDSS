#include "viewmodels/MetricDefinitionListModel.hpp"

#include "viewmodels/MetricTexts.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

MetricDefinitionListModel::MetricDefinitionListModel(QObject* parent) : QAbstractListModel(parent) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int MetricDefinitionListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_definitions.size());
}

QVariant MetricDefinitionListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const services::MetricDefinitionInfo& info =
        m_definitions[static_cast<std::size_t>(index.row())];
    const core::MetricDefinition& metric = info.definition;
    switch (role) {
    case MetricIdRole:
    case ValueRole:
        return QVariant::fromValue<qint64>(metric.id.value);
    case KeyRole:
        return QString::fromStdString(metric.key);
    case NameRole:
        return MetricTexts::name(metric);
    case NamePlRole:
        return QString::fromStdString(metric.namePl);
    case NameEnRole:
        return QString::fromStdString(metric.nameEn);
    case UnitRole:
        return metric.unit ? QString::fromStdString(*metric.unit) : QString{};
    case ValueTypeRole:
        return QString::fromUtf8(core::toDbText(metric.valueType));
    case ValueTypeTextRole:
        return MetricTexts::valueTypeName(metric.valueType);
    case AggregationRole:
        return QString::fromUtf8(core::toDbText(metric.aggregation));
    case AggregationTextRole:
        return MetricTexts::aggregationName(metric.aggregation);
    case ScopeRole:
        return QString::fromUtf8(core::toDbText(metric.scope));
    case ScopeTextRole:
        return MetricTexts::scopeName(metric.scope);
    case HigherIsBetterRole:
        return metric.higherIsBetter;
    case MinTextRole:
        return MetricTexts::limitText(metric.min);
    case MaxTextRole:
        return MetricTexts::limitText(metric.max);
    case RangeTextRole:
        return MetricTexts::range(metric);
    case ValueCountRole:
        return info.valueCount;
    case TextRole:
        return MetricTexts::pickerText(metric);
    default:
        return {};
    }
}

QHash<int, QByteArray> MetricDefinitionListModel::roleNames() const {
    return {{MetricIdRole, "metricId"},
            {KeyRole, "key"},
            {NameRole, "name"},
            {NamePlRole, "namePl"},
            {NameEnRole, "nameEn"},
            {UnitRole, "unit"},
            {ValueTypeRole, "valueType"},
            {ValueTypeTextRole, "valueTypeText"},
            {AggregationRole, "aggregation"},
            {AggregationTextRole, "aggregationText"},
            {ScopeRole, "scope"},
            {ScopeTextRole, "scopeText"},
            {HigherIsBetterRole, "higherIsBetter"},
            {MinTextRole, "minText"},
            {MaxTextRole, "maxText"},
            {RangeTextRole, "rangeText"},
            {ValueCountRole, "valueCount"},
            {TextRole, "text"},
            {ValueRole, "value"}};
}

int MetricDefinitionListModel::count() const { return rowCount(); }

void MetricDefinitionListModel::setDefinitions(
    std::vector<services::MetricDefinitionInfo> definitions) {
    const int before = rowCount();
    beginResetModel();
    m_definitions = std::move(definitions);
    endResetModel();
    if (rowCount() != before) {
        emit countChanged();
    }
}

const services::MetricDefinitionInfo* MetricDefinitionListModel::find(qint64 metricId) const {
    const auto found =
        std::ranges::find_if(m_definitions, [metricId](const services::MetricDefinitionInfo& info) {
            return info.definition.id.value == metricId;
        });
    return found == m_definitions.end() ? nullptr : &*found;
}

QVariantMap MetricDefinitionListModel::rowAt(int row) const {
    QVariantMap result;
    if (row < 0 || row >= rowCount()) {
        return result;
    }
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        result.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    }
    return result;
}

int MetricDefinitionListModel::rowOf(qint64 metricId) const {
    const auto found =
        std::ranges::find_if(m_definitions, [metricId](const services::MetricDefinitionInfo& info) {
            return info.definition.id.value == metricId;
        });
    return found == m_definitions.end()
               ? -1
               : static_cast<int>(std::distance(m_definitions.begin(), found));
}

bool MetricDefinitionListModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange &&
        !m_definitions.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1));
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
