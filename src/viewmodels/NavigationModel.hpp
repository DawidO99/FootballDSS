#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <vector>

namespace fdss::viewmodels {

// Sections of the main window and the currently selected one. The sidebar shows the first
// sidebarCount of them; the others (the match report) are opened from a view with arguments.
class NavigationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged FINAL)
    Q_PROPERTY(QString currentKey READ currentKey NOTIFY currentIndexChanged FINAL)
    Q_PROPERTY(QVariantMap currentArgs READ currentArgs NOTIFY currentIndexChanged FINAL)
    // The sections with a sidebar item; they come first.
    Q_PROPERTY(int sidebarCount READ sidebarCount CONSTANT FINAL)
    // A view holds changes that are not saved. While on, moving elsewhere only remembers the target
    // and emits leaveRequested(), whichever way the move came: the sidebar writes currentIndex
    // directly, so a view could not stop it on its own.
    Q_PROPERTY(bool leaveGuard READ leaveGuard WRITE setLeaveGuard NOTIFY leaveGuardChanged FINAL)
    // Capability key -> hint of the locked capabilities (CapabilityModel.locks, packet P15). A
    // section that needs a locked capability stays in the sidebar with a lock and shows the hint.
    Q_PROPERTY(QVariantMap locks READ locks WRITE setLocks NOTIFY locksChanged FINAL)
    Q_PROPERTY(bool currentLocked READ currentLocked NOTIFY currentLockChanged FINAL)
    // The capability the current section needs; empty when it needs none.
    Q_PROPERTY(QString currentCapability READ currentCapability NOTIFY currentIndexChanged FINAL)

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        IconNameRole,
        InSidebarRole,
        CapabilityRole,
        LockedRole,
        LockHintRole
    };

    explicit NavigationModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int currentIndex() const;
    // Moving to another section this way (the sidebar) clears the arguments.
    void setCurrentIndex(int index);
    [[nodiscard]] QString currentKey() const;
    [[nodiscard]] QVariantMap currentArgs() const;
    [[nodiscard]] int sidebarCount() const;

    // Selects the section with the given key and hands it the arguments; unknown keys are reported
    // and ignored. New arguments for the current section also emit currentIndexChanged, the notify
    // signal of currentArgs.
    Q_INVOKABLE void activate(const QString& key, const QVariantMap& args = {});

    [[nodiscard]] bool leaveGuard() const;
    // Turning it off keeps a remembered target, for resumeLeave() after the save that ended the
    // guard.
    void setLeaveGuard(bool guard);
    // Turns the guard off and goes to the remembered target.
    Q_INVOKABLE void resumeLeave();
    // Forgets the target. Notifies, so a sidebar that has already moved its own index comes back.
    Q_INVOKABLE void cancelLeave();

    [[nodiscard]] QVariantMap locks() const;
    void setLocks(const QVariantMap& locks);
    [[nodiscard]] bool currentLocked() const;
    [[nodiscard]] QString currentCapability() const;

signals:
    void currentIndexChanged();
    void leaveGuardChanged();
    void leaveRequested();
    void locksChanged();
    void currentLockChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Item {
        QString key;
        const char* title; // untranslated source text, translated in data()
        QString iconName;
        bool inSidebar = true;
        // CapabilityModel::key() of what the section needs; empty: nothing (P15 decision 2).
        QString capability;
    };

    struct Target {
        int index = 0;
        QVariantMap args;
    };

    // Moves there, or remembers it while the guard is on.
    void moveTo(int index, const QVariantMap& args);
    [[nodiscard]] bool isLocked(const Item& item) const;

    std::vector<Item> m_items;
    int m_currentIndex = 0;
    QVariantMap m_currentArgs;
    bool m_leaveGuard = false;
    std::optional<Target> m_pendingTarget;
    QVariantMap m_locks;
};

} // namespace fdss::viewmodels
