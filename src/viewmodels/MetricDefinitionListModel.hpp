#pragma once

#include "services/MetricService.hpp"

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::viewmodels {

// The custom metric definitions, in the order they were created, for the Settings card and the
// metric picker of the Metrics section.
class MetricDefinitionListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        MetricIdRole = Qt::UserRole + 1,
        KeyRole,
        NameRole, // in the app language, see MetricTexts::name()
        NamePlRole,
        NameEnRole,
        UnitRole, // empty without one
        ValueTypeRole,
        ValueTypeTextRole,
        AggregationRole,
        AggregationTextRole,
        ScopeRole,
        ScopeTextRole,
        HigherIsBetterRole,
        MinTextRole, // as the dialog shows it; empty for no limit
        MaxTextRole,
        RangeTextRole,
        ValueCountRole,
        // [{ value, text }] for a SelectField: the id and the name with its unit.
        TextRole,
        ValueRole
    };

    explicit MetricDefinitionListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    void setDefinitions(std::vector<services::MetricDefinitionInfo> definitions);
    // nullptr when no row has the id.
    [[nodiscard]] const services::MetricDefinitionInfo* find(qint64 metricId) const;

    // The row under the role names; empty out of range. For the views and the tests.
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    // -1 when no row has the id.
    Q_INVOKABLE int rowOf(qint64 metricId) const;

signals:
    void countChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<services::MetricDefinitionInfo> m_definitions;
};

} // namespace fdss::viewmodels
