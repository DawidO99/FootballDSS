#include "viewmodels/ProfilePickerViewModel.hpp"

#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <utility>

namespace fdss::viewmodels {

ProfilePickerViewModel::ProfilePickerViewModel(QObject* parent)
    : QObject(parent), m_auth(ServiceRegistry::instance().auth()),
      m_lock(ServiceRegistry::instance().clock()), m_staff(new StaffListModel(this)) {
    connect(&m_lock, &LockCountdown::changed, this, &ProfilePickerViewModel::messageChanged);
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    reload();
}

StaffListModel* ProfilePickerViewModel::staff() const { return m_staff; }

qint64 ProfilePickerViewModel::selectedId() const { return m_selectedId; }

QString ProfilePickerViewModel::selectedName() const {
    const core::StaffUser* user = m_staff->find(m_selectedId);
    return user != nullptr ? QString::fromStdString(user->name) : QString{};
}

QString ProfilePickerViewModel::message() const {
    if (m_lock.locked()) {
        return m_lock.message();
    }
    if (m_malformedPin) {
        return ErrorPresenter::invalidInputMessage("pin");
    }
    return m_wrongPin ? ErrorPresenter::pinCheckMessage(*m_wrongPin) : QString{};
}

bool ProfilePickerViewModel::locked() const { return m_lock.locked(); }

QString ProfilePickerViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void ProfilePickerViewModel::reload() {
    auto staff = m_auth.listActiveStaff();
    if (staff) {
        m_staff->setStaff(std::move(*staff));
        m_loadFailure.reset();
    } else {
        qCritical("ProfilePickerViewModel: %s", staff.error().detail.c_str());
        m_staff->setStaff({});
        m_loadFailure = staff.error();
    }
    emit loadErrorChanged();

    const auto& users = m_staff->staff();
    if (users.size() == 1) {
        select(users.front().id.value);
    } else if (m_staff->find(m_selectedId) == nullptr) {
        select(0);
    } else {
        emit selectionChanged(); // the name may have changed
    }
}

void ProfilePickerViewModel::select(qint64 id) {
    if (id != 0 && m_staff->find(id) == nullptr) {
        qWarning("ProfilePickerViewModel: unknown staff user %lld", static_cast<long long>(id));
        return;
    }
    if (m_selectedId != id) {
        m_selectedId = id;
        m_lock.cancel();
        forgetAttempt();
        emit messageChanged();
        emit selectionChanged();
    }
    readLock();
}

void ProfilePickerViewModel::login(const QString& pin) {
    if (m_selectedId == 0) {
        return;
    }
    const auto check = m_auth.login(core::StaffUserId{m_selectedId}, pin.toStdString());
    if (!check) {
        if (check.error().code == core::ErrorCode::InvalidInput) {
            forgetAttempt();
            m_malformedPin = true;
            emit messageChanged();
            return;
        }
        // Deactivated or gone since the list was read: show the list as it is now.
        report(check.error());
        reload();
        return;
    }
    forgetAttempt();
    switch (check->status) {
    case services::PinCheckStatus::Accepted:
        emit messageChanged();
        emit loggedIn();
        return;
    case services::PinCheckStatus::WrongPin:
        m_wrongPin = *check;
        emit messageChanged();
        break;
    case services::PinCheckStatus::LockedOut:
        m_lock.start(check->lockedFor);
        break;
    }
    emit pinRejected();
}

void ProfilePickerViewModel::tickLock() { m_lock.tick(); }

bool ProfilePickerViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit messageChanged();
        emit loadErrorChanged();
    }
    return QObject::eventFilter(watched, event);
}

void ProfilePickerViewModel::readLock() {
    if (m_selectedId == 0) {
        return;
    }
    const auto left = m_auth.lockRemaining(core::StaffUserId{m_selectedId});
    if (!left) {
        // A countdown already running goes on: it needs no database.
        report(left.error());
        return;
    }
    m_lock.start(*left);
}

void ProfilePickerViewModel::forgetAttempt() {
    m_wrongPin.reset();
    m_malformedPin = false;
}

void ProfilePickerViewModel::report(const core::Error& error) {
    qCritical("ProfilePickerViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
