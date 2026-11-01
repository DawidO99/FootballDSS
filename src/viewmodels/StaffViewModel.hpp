#pragma once

#include "core/util/Error.hpp"
#include "viewmodels/StaffListModel.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace fdss::services {
class AuthService;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// Settings -> Staff. Needs ManageStaff: without it the list stays empty, and the services refuse
// the changes.
class StaffViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(fdss::viewmodels::StaffListModel* staff READ staff CONSTANT FINAL)
    // [{ value, text }]: the database text of a core Role and its translated name.
    Q_PROPERTY(QVariantList roleOptions READ roleOptions NOTIFY roleOptionsChanged FINAL)
    // Main.qml builds the views anew for every login, so this holds for the life of the view.
    Q_PROPERTY(qint64 currentUserId READ currentUserId CONSTANT FINAL)
    // Why the list could not be read; the view offers to try again. Empty: the list is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)
    Q_PROPERTY(QString nameError READ nameError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString pinError READ pinError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString pinRepeatError READ pinRepeatError NOTIFY errorsChanged FINAL)

public:
    explicit StaffViewModel(QObject* parent = nullptr);

    [[nodiscard]] StaffListModel* staff() const;
    [[nodiscard]] QVariantList roleOptions() const;
    [[nodiscard]] qint64 currentUserId() const;
    [[nodiscard]] QString loadError() const;
    [[nodiscard]] QString nameError() const;
    [[nodiscard]] QString pinError() const;
    [[nodiscard]] QString pinRepeatError() const;

    Q_INVOKABLE void reload();
    // `role` is the database text of a core Role. True when the change was made; otherwise the
    // error properties or errorOccurred say why.
    Q_INVOKABLE bool add(const QString& name, const QString& role, const QString& pin,
                         const QString& pinRepeat);
    Q_INVOKABLE bool update(qint64 id, const QString& name, const QString& role);
    Q_INVOKABLE bool resetPin(qint64 id, const QString& pin, const QString& pinRepeat);
    Q_INVOKABLE bool deactivate(qint64 id);
    Q_INVOKABLE bool reactivate(qint64 id);
    Q_INVOKABLE void clearErrors();

signals:
    void roleOptionsChanged();
    void loadErrorChanged();
    void errorsChanged();
    // After every change. The view calls AppState.refresh(): the change may have renamed the
    // session user or ended the session.
    void staffChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] bool checkName(const QString& name);
    [[nodiscard]] bool checkPin(const QString& pin, const QString& pinRepeat);
    [[nodiscard]] bool finish(const core::Status& status);
    void report(const core::Error& error);

    services::AuthService& m_auth;
    services::Session& m_session;
    StaffListModel* m_staff;
    qint64 m_currentUserId = 0;

    QString m_loadError;
    QString m_nameError;
    QString m_pinError;
    QString m_pinRepeatError;
};

} // namespace fdss::viewmodels
