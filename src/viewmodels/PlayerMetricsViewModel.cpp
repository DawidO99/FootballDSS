#include "viewmodels/PlayerMetricsViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/MetricTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <utility>

namespace fdss::viewmodels {

namespace {

QString trendKey(core::MetricTrend trend) {
    switch (trend) {
    case core::MetricTrend::Up:
        return QStringLiteral("up");
    case core::MetricTrend::Down:
        return QStringLiteral("down");
    case core::MetricTrend::Same:
        return QStringLiteral("same");
    case core::MetricTrend::None:
        break;
    }
    return QStringLiteral("none");
}

QString shown(const core::MetricDefinition& metric, double value) {
    const QString text = MetricTexts::value(metric, value);
    return metric.valueType == core::MetricValueType::Boolean
               ? text
               : MetricTexts::withUnit(text, metric.unit);
}

} // namespace

PlayerMetricsViewModel::PlayerMetricsViewModel(QObject* parent)
    : QAbstractListModel(parent), m_metrics(ServiceRegistry::instance().metrics()),
      m_session(ServiceRegistry::instance().session()) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int PlayerMetricsViewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant PlayerMetricsViewModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const services::PlayerMetricSummary& row = m_rows[static_cast<std::size_t>(index.row())];
    const bool improvement = core::isImprovement(row.trend, row.metric.higherIsBetter);
    switch (role) {
    case MetricIdRole:
        return QVariant::fromValue<qint64>(row.metric.id.value);
    case NameRole:
        return MetricTexts::name(row.metric);
    case LatestTextRole:
        return shown(row.metric, row.latest);
    case LatestDateRole:
        return IsoDates::toIso(row.latestDate);
    case LatestDateTextRole:
        return IsoDates::displayText(row.latestDate);
    case WindowTextRole:
        if (!row.window) {
            return tr("No value in the last %n day(s)", nullptr, core::kMetricWindowDays);
        }
        return MetricTexts::window(row.metric, *row.window, row.windowCount);
    case TrendRole:
        return trendKey(row.trend);
    case ImprovementRole:
        return improvement;
    case TrendTextRole:
        switch (row.trend) {
        case core::MetricTrend::Up:
        case core::MetricTrend::Down:
            return improvement ? tr("Better") : tr("Worse");
        case core::MetricTrend::Same:
            return tr("No change");
        case core::MetricTrend::None:
            break;
        }
        return QString{};
    case PreviousTextRole:
        return row.previous ? tr("Previous value: %1").arg(shown(row.metric, *row.previous))
                            : tr("No previous value");
    default:
        return {};
    }
}

QHash<int, QByteArray> PlayerMetricsViewModel::roleNames() const {
    return {{MetricIdRole, "metricId"},
            {NameRole, "name"},
            {LatestTextRole, "latestText"},
            {LatestDateRole, "latestDate"},
            {LatestDateTextRole, "latestDateText"},
            {WindowTextRole, "windowText"},
            {TrendRole, "trend"},
            {ImprovementRole, "improvement"},
            {TrendTextRole, "trendText"},
            {PreviousTextRole, "previousText"}};
}

qint64 PlayerMetricsViewModel::playerId() const { return m_playerId; }

void PlayerMetricsViewModel::setPlayerId(qint64 playerId) {
    if (playerId == m_playerId) {
        return;
    }
    m_playerId = playerId;
    emit playerIdChanged();
    reload();
}

int PlayerMetricsViewModel::count() const { return rowCount(); }

bool PlayerMetricsViewModel::canManage() const {
    return m_session.isAllowed(core::Permission::ManageMetrics);
}

QString PlayerMetricsViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void PlayerMetricsViewModel::reload() {
    const bool hadFailure = m_loadFailure.has_value();
    m_loadFailure.reset();
    if (m_playerId == 0 || !m_session.user()) {
        setRows({});
    } else if (auto summary = m_metrics.playerSummary(core::PlayerId{m_playerId}); summary) {
        setRows(std::move(*summary));
    } else {
        qCritical("PlayerMetricsViewModel: %s", summary.error().detail.c_str());
        m_loadFailure = summary.error();
        setRows({});
    }
    if (hadFailure != m_loadFailure.has_value()) {
        emit loadErrorChanged();
    }
}

bool PlayerMetricsViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit loadErrorChanged();
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1));
        }
    }
    return QAbstractListModel::eventFilter(watched, event);
}

void PlayerMetricsViewModel::setRows(std::vector<services::PlayerMetricSummary> rows) {
    const int before = rowCount();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (rowCount() != before) {
        emit countChanged();
    }
}

} // namespace fdss::viewmodels
