#pragma once

#include "viewmodels/CalendarTexts.hpp"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::viewmodels {

// The list mode of the calendar: upcoming items from the nearest on, then past items from the most
// recent back. Within one day always by time, items without a time last.
class CalendarListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        KindRole = Qt::UserRole + 1,
        ItemIdRole,
        DateRole,
        DateTextRole,
        TimeRole,
        KindTextRole,
        TitleRole,
        DetailsRole,
        StatusKeyRole,
        StatusTextRole,
        // The heading of the group, translated.
        SectionRole
    };

    explicit CalendarListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // today: where upcoming ends. A date of today is upcoming, whatever the time.
    void setEntries(std::vector<CalendarEntry> entries, core::Date today);

    // The order of two items of one day: by time, without a time last, a match before a session on
    // a full tie.
    [[nodiscard]] static bool earlierOnTheDay(const CalendarEntry& a, const CalendarEntry& b);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<CalendarEntry> m_entries;
    core::Date m_today{};
};

} // namespace fdss::viewmodels
