#pragma once

#include "core/util/Error.hpp"
#include "services/MetricService.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <vector>

namespace fdss::services {
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The metrics card on the Overview of the player card (packet P17): per metric with a value, the
// latest value, the aggregation over the last 28 days and the direction against the previous value.
class PlayerMetricsViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    // 0: no player.
    Q_PROPERTY(qint64 playerId READ playerId WRITE setPlayerId NOTIFY playerIdChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    // The empty state offers entering values.
    Q_PROPERTY(bool canManage READ canManage CONSTANT FINAL)
    // Why the metrics could not be read; the view offers to try again. Empty: they are fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        MetricIdRole = Qt::UserRole + 1,
        NameRole,
        // "4,35 s"; "Yes" / "No" for a boolean metric.
        LatestTextRole,
        LatestDateRole, // ISO yyyy-MM-dd
        LatestDateTextRole,
        // The aggregation over the window, or that there is no value in it.
        WindowTextRole,
        TrendRole, // up, down, same or none
        // The change goes the way "higher is better" says; false for same and none.
        ImprovementRole,
        // The word next to the arrow: "Better", "Worse", "No change"; empty for none.
        TrendTextRole,
        // For the tooltip and the accessible name, e.g. "Previous value: 4,40 s".
        PreviousTextRole
    };

    explicit PlayerMetricsViewModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] qint64 playerId() const;
    void setPlayerId(qint64 playerId);
    [[nodiscard]] int count() const;
    [[nodiscard]] bool canManage() const;
    [[nodiscard]] QString loadError() const;

    Q_INVOKABLE void reload();

signals:
    void playerIdChanged();
    void countChanged();
    void loadErrorChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setRows(std::vector<services::PlayerMetricSummary> rows);

    services::MetricService& m_metrics;
    services::Session& m_session;
    qint64 m_playerId = 0;
    std::vector<services::PlayerMetricSummary> m_rows;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
