#pragma once

#include "core/algorithms/Load.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/util/Error.hpp"
#include "viewmodels/LoadFilterModel.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class LoadService;
class Session;
struct TeamLoad;
} // namespace fdss::services

namespace fdss::viewmodels {

class LoadListModel;

// The team table of the Load screen (SPEC section 4, packet P11): status per player, filter, sort.
// Read-only, every role (ViewAnalysis). The service reads on the main thread and computes on the
// thread pool; busy is true until the result is in, and a result of an older request is dropped.
class LoadViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no team, and an empty table. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::LoadFilterModel* players READ players CONSTANT FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    // all, high, elevated, normal, underloaded or notEnoughData.
    Q_PROPERTY(
        QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY filtersChanged FINAL)
    // [{ value, text }], the text with the number of players.
    Q_PROPERTY(
        QVariantList statusFilterOptions READ statusFilterOptions NOTIFY optionsChanged FINAL)
    // The filter narrows the table, so an empty one means no match, not no players.
    Q_PROPERTY(bool filtersActive READ filtersActive NOTIFY filtersChanged FINAL)
    // A role name of the table.
    Q_PROPERTY(QString sortRole READ sortRole NOTIFY sortChanged FINAL)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged FINAL)
    // Players before the filter.
    Q_PROPERTY(int playerCount READ playerCount NOTIFY playersReloaded FINAL)
    // While nobody has a status yet: what to enter, e.g. "Days with RPE still needed: 11." Else
    // empty.
    Q_PROPERTY(QString teamHint READ teamHint NOTIFY textsChanged FINAL)
    // "As of …": the most recent day the statuses describe. Empty without players.
    Q_PROPERTY(QString asOfText READ asOfText NOTIFY textsChanged FINAL)
    // Why the table could not be read; the view offers to try again. Empty: the table is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    explicit LoadViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] LoadFilterModel* players() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString statusFilter() const;
    void setStatusFilter(const QString& filter);
    [[nodiscard]] QVariantList statusFilterOptions() const;
    [[nodiscard]] bool filtersActive() const;
    [[nodiscard]] QString sortRole() const;
    [[nodiscard]] bool sortAscending() const;
    [[nodiscard]] int playerCount() const;
    [[nodiscard]] QString teamHint() const;
    [[nodiscard]] QString asOfText() const;
    [[nodiscard]] QString loadError() const;

    Q_INVOKABLE void reload();
    // An unknown role is reported and ignored.
    Q_INVOKABLE void sortBy(const QString& role, bool ascending);
    Q_INVOKABLE void clearFilters();
    // For a row of the table: its values under the role names, empty when out of range.
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    // -1 when the player is not in the table, for example filtered out.
    Q_INVOKABLE int rowOf(qint64 playerId) const;

signals:
    void teamIdChanged();
    void busyChanged();
    void filtersChanged();
    void sortChanged();
    void optionsChanged();
    void loadErrorChanged();
    // After a new result is in. The view selects the same player again.
    void playersReloaded();
    // teamHint and asOfText: after a result and on a change of the language.
    void textsChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void apply(services::TeamLoad result);
    void clearRows(std::optional<core::Error> failure);
    void setBusy(bool busy);

    services::LoadService& m_load;
    services::Session& m_session;
    LoadListModel* m_source;
    LoadFilterModel* m_filter;

    qint64 m_teamId = 0;
    bool m_busy = false;
    // Every request gets the next number; only the result of the newest one is shown.
    int m_request = 0;
    QString m_statusFilter;
    QString m_sortRole;
    bool m_sortAscending = true;
    std::optional<core::Date> m_asOf;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
