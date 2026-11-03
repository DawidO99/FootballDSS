#pragma once

#include "viewmodels/CalendarTexts.hpp"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <array>
#include <vector>

namespace fdss::viewmodels {

// The week mode of the calendar: always seven rows, Monday to Sunday.
class CalendarWeekModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    static constexpr int kDaysInWeek = 7;

    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        DateRole = Qt::UserRole + 1,
        DayNameRole,
        DateTextRole,
        TodayRole,
        // [{ kind, itemId, time, kindText, title, details, statusKey, statusText }], by time.
        ItemsRole
    };

    explicit CalendarWeekModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Keeps the entries of the week that starts on this Monday.
    void setWeek(core::Date monday, const std::vector<CalendarEntry>& entries, core::Date today);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Day {
        core::Date date{};
        std::vector<CalendarEntry> entries;
    };

    std::array<Day, kDaysInWeek> m_days;
    core::Date m_today{};
};

} // namespace fdss::viewmodels
