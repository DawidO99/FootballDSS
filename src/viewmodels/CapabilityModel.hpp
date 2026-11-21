#pragma once

#include "core/algorithms/Capabilities.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::services {
class CapabilityService;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The capabilities of SPEC section 2 for the team (packet P15): one row per capability that is not
// hidden, with what it unlocks and, while locked, what to enter and where. Main.qml holds one and
// reloads it on every login, section change and change of showAdvanced (P15 decision 5); the
// sidebar takes the locked ones through NavigationModel.locks.
class CapabilityModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(
        bool showAdvanced READ showAdvanced WRITE setShowAdvanced NOTIFY showAdvancedChanged FINAL)
    // Capability key -> hint, for the locked ones only.
    Q_PROPERTY(QVariantMap locks READ locks NOTIFY capabilitiesChanged FINAL)
    // Capability key -> a map of the role names, for every capability, hidden ones too; a binding
    // on it follows every read and language change.
    Q_PROPERTY(QVariantMap byKey READ byKey NOTIFY capabilitiesChanged FINAL)
    // False until the first read succeeded, and after a failed one.
    Q_PROPERTY(bool ready READ ready NOTIFY capabilitiesChanged FINAL)

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        // available or locked (hidden only through byKey)
        StateKeyRole,
        StateTextRole,
        // What it unlocks.
        DescriptionRole,
        // Locked: what is missing, counted. Available: a note (Load before 28 days) or empty.
        HintRole,
        // The section where the missing data is entered.
        ActionSectionRole,
        // Empty for a role that cannot enter it; askText is set then.
        ActionTextRole,
        AskTextRole
    };

    explicit CapabilityModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] bool showAdvanced() const;
    void setShowAdvanced(bool show);
    [[nodiscard]] QVariantMap locks() const;
    [[nodiscard]] QVariantMap byKey() const;
    [[nodiscard]] bool ready() const;

    // Reads the counters anew. Without a logged-in user or a team: no rows. A failed read is
    // reported and leaves no rows, so nothing is locked by an error.
    Q_INVOKABLE void reload();
    // Every role of one capability by key, also a hidden one; {} for an unknown key.
    [[nodiscard]] QVariantMap capability(const QString& key) const;

    [[nodiscard]] static QString key(core::CapabilityKind kind);

signals:
    void teamIdChanged();
    void showAdvancedChanged();
    void capabilitiesChanged();
    // A translated text for the user; Main.qml shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] QVariant value(const core::Capability& capability, int role) const;
    [[nodiscard]] const core::Capability* find(const QString& key) const;
    [[nodiscard]] QVariantMap values(const core::Capability& capability) const;
    [[nodiscard]] bool canEnter(core::CapabilityKind kind) const;

    services::CapabilityService& m_service;
    services::Session& m_session;
    qint64 m_teamId = 0;
    bool m_showAdvanced = false;
    bool m_ready = false;
    // Every kind, hidden ones too; the rows are the ones not hidden.
    std::vector<core::Capability> m_all;
    std::vector<core::Capability> m_rows;
};

} // namespace fdss::viewmodels
