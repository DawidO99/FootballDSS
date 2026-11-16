#pragma once

#include "core/algorithms/Load.hpp"
#include "services/LoadService.hpp"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <vector>

namespace fdss::viewmodels {

// The team table of the Load screen, one row per player who is not archived. LoadFilterModel sits
// on top of it. A grey row shows the acute load only: the chronic load, the ratio and the monotony
// are null until there is a baseline.
class LoadListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        PlayerIdRole = Qt::UserRole + 1,
        NumberRole,
        NameRole,
        // Numbers or null; the *Text roles are what the table shows.
        AcuteRole,
        AcuteTextRole,
        ChronicRole,
        ChronicTextRole,
        RatioRole,
        RatioTextRole,
        MonotonyRole,
        MonotonyTextRole,
        WeekLoadRole,
        WeekLoadTextRole,
        // What LoadStatusBadge takes; the full word (tooltip, accessible name) and the short one
        // the table shows.
        StatusKeyRole,
        StatusTextRole,
        StatusShortTextRole,
        // 0 high … 4 not enough data.
        StatusRankRole,
        // What is missing for a status; empty when nothing is.
        HintRole,
        // Why the status is what it is (tooltip).
        ExplanationRole,
        // Only when the row describes another day than the table (P11 review): the day and month
        // for the cell, and "As of …" for the tooltip and the accessible name. Empty otherwise.
        AsOfShortRole,
        AsOfTextRole,
        // What drove the status, independent of the language (LoadTexts::reasonKey).
        ReasonKeysRole
    };

    explicit LoadListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // tableAsOf: the day the table header names; rows with data on another day say so.
    void setRows(std::vector<services::PlayerLoadRow> rows, const core::LoadThresholds& thresholds,
                 std::optional<core::Date> tableAsOf);
    [[nodiscard]] const services::PlayerLoadRow& rowAt(int row) const;
    // -1 when the id is not in the list.
    [[nodiscard]] int rowOf(qint64 playerId) const;
    [[nodiscard]] const std::vector<services::PlayerLoadRow>& rows() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<services::PlayerLoadRow> m_rows;
    core::LoadThresholds m_thresholds;
    std::optional<core::Date> m_tableAsOf;
};

} // namespace fdss::viewmodels
