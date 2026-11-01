#pragma once

#include "core/util/Error.hpp"
#include "viewmodels/SquadFilterModel.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class Clock;
class Session;
class SquadService;
} // namespace fdss::services

namespace fdss::viewmodels {

class SquadListModel;

// The squad table: what it shows, how it is filtered and sorted, and archiving.
class SquadViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no team, and an empty list. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::SquadFilterModel* players READ players CONSTANT FINAL)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filtersChanged FINAL)
    // all, available or unavailable.
    Q_PROPERTY(
        QString statusGroup READ statusGroup WRITE setStatusGroup NOTIFY filtersChanged FINAL)
    // all, goalkeeper, defender, midfielder or forward.
    Q_PROPERTY(
        QString positionGroup READ positionGroup WRITE setPositionGroup NOTIFY filtersChanged FINAL)
    Q_PROPERTY(
        bool showArchived READ showArchived WRITE setShowArchived NOTIFY filtersChanged FINAL)
    // The search or a group narrows the list, so an empty table means no match, not no players.
    Q_PROPERTY(bool filtersActive READ filtersActive NOTIFY filtersChanged FINAL)
    // A role name of the table.
    Q_PROPERTY(QString sortRole READ sortRole NOTIFY sortChanged FINAL)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged FINAL)
    // [{ value, text }]
    Q_PROPERTY(QVariantList statusGroupOptions READ statusGroupOptions NOTIFY optionsChanged FINAL)
    Q_PROPERTY(
        QVariantList positionGroupOptions READ positionGroupOptions NOTIFY optionsChanged FINAL)
    // Why the list could not be read; the view offers to try again. Empty: the list is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    explicit SquadViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] SquadFilterModel* players() const;
    [[nodiscard]] QString searchText() const;
    void setSearchText(const QString& text);
    [[nodiscard]] QString statusGroup() const;
    void setStatusGroup(const QString& group);
    [[nodiscard]] QString positionGroup() const;
    void setPositionGroup(const QString& group);
    [[nodiscard]] bool showArchived() const;
    void setShowArchived(bool show);
    [[nodiscard]] bool filtersActive() const;
    [[nodiscard]] QString sortRole() const;
    [[nodiscard]] bool sortAscending() const;
    [[nodiscard]] QVariantList statusGroupOptions() const;
    [[nodiscard]] QVariantList positionGroupOptions() const;
    [[nodiscard]] QString loadError() const;

    Q_INVOKABLE void reload();
    // An unknown role is reported and ignored.
    Q_INVOKABLE void sortBy(const QString& role, bool ascending);
    // The search and the groups; "Show archived" stays.
    Q_INVOKABLE void clearFilters();
    // For a row of the table: its values under the role names, empty when out of range.
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    // -1 when the player is not in the table, for example filtered out.
    Q_INVOKABLE int rowOf(qint64 playerId) const;
    // True when the change was made; otherwise errorOccurred says why.
    Q_INVOKABLE bool archive(qint64 playerId);
    Q_INVOKABLE bool unarchive(qint64 playerId);

signals:
    void teamIdChanged();
    void filtersChanged();
    void sortChanged();
    void optionsChanged();
    void loadErrorChanged();
    // After the list was read anew. The view selects the same player again.
    void playersReloaded();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Without EditSquad: errorOccurred, and the service is not called. The service checks the same
    // again (AGENTS section 4).
    [[nodiscard]] bool checkAllowed();
    [[nodiscard]] bool finish(const core::Status& status);
    void report(const core::Error& error);

    services::SquadService& m_squad;
    services::Session& m_session;
    const services::Clock& m_clock;
    SquadListModel* m_source;
    SquadFilterModel* m_filter;

    qint64 m_teamId = 0;
    QString m_searchText;
    QString m_statusGroup;
    QString m_positionGroup;
    bool m_showArchived = false;
    QString m_sortRole;
    bool m_sortAscending = true;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
