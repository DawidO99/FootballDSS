#include "viewmodels/ProfilePickerViewModel.hpp"

#include "services/AuthService.hpp"
#include "services/Clock.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

namespace {

constexpr std::chrono::seconds kLockTick{1};

} // namespace

ProfilePickerViewModel::ProfilePickerViewModel(QObject* parent)
    : QObject(parent), m_auth(ServiceRegistry::instance().auth()),
      m_clock(ServiceRegistry::instance().clock()), m_staff(new StaffListModel(this)) {
    m_lockTimer.setInterval(kLockTick);
    connect(&m_lockTimer, &QTimer::timeout, this, &ProfilePickerViewModel::tickLock);
    reload();
}

StaffListModel* ProfilePickerViewModel::staff() const { return m_staff; }

qint64 ProfilePickerViewModel::selectedId() const { return m_selectedId; }

QString ProfilePickerViewModel::selectedName() const {
    const core::StaffUser* user = m_staff->find(m_selectedId);
    return user != nullptr ? QString::fromStdString(user->name) : QString{};
}

QString ProfilePickerViewModel::message() const { return m_message; }

bool ProfilePickerViewModel::locked() const { return m_lockSeconds > 0; }

QString ProfilePickerViewModel::loadError() const { return m_loadError; }

void ProfilePickerViewModel::reload() {
    auto staff = m_auth.listActiveStaff();
    const QString loadError = staff ? QString{} : ErrorPresenter::message(staff.error());
    if (staff) {
        m_staff->setStaff(std::move(*staff));
    } else {
        qCritical("ProfilePickerViewModel: %s", staff.error().detail.c_str());
        m_staff->setStaff({});
    }
    if (m_loadError != loadError) {
        m_loadError = loadError;
        emit loadErrorChanged();
    }

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
        m_lockSeconds = 0;
        m_lockTimer.stop();
        setMessage({});
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
            setMessage(ErrorPresenter::message(check.error()));
            return;
        }
        // Deactivated or gone since the list was read: show the list as it is now.
        report(check.error());
        reload();
        return;
    }
    switch (check->status) {
    case services::PinCheckStatus::Accepted:
        setMessage({});
        emit loggedIn();
        return;
    case services::PinCheckStatus::WrongPin:
        setMessage(ErrorPresenter::pinCheckMessage(*check));
        break;
    case services::PinCheckStatus::LockedOut:
        startLock(check->lockedFor);
        break;
    }
    emit pinRejected();
}

void ProfilePickerViewModel::tickLock() {
    if (m_lockSeconds <= 0) {
        return;
    }
    const std::chrono::seconds left = m_lockEnd - m_clock.now();
    // More than a whole lockout left means the clock was moved back; the service treats that as
    // expired too (P07a decision 8).
    showLock(left > services::kLockoutDuration ? 0 : std::max<qint64>(left.count(), 0));
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
    startLock(*left);
}

void ProfilePickerViewModel::startLock(std::chrono::seconds left) {
    m_lockEnd = m_clock.now() + left;
    showLock(left.count());
}

void ProfilePickerViewModel::showLock(qint64 seconds) {
    const bool wasLocked = m_lockSeconds > 0;
    m_lockSeconds = seconds;
    if (m_lockSeconds > 0) {
        services::PinCheck check;
        check.status = services::PinCheckStatus::LockedOut;
        check.lockedFor = std::chrono::seconds{m_lockSeconds};
        m_message = ErrorPresenter::pinCheckMessage(check);
        if (!m_lockTimer.isActive()) {
            m_lockTimer.start();
        }
        emit messageChanged();
    } else if (wasLocked) {
        m_lockTimer.stop();
        m_message.clear();
        emit messageChanged();
    }
}

void ProfilePickerViewModel::setMessage(const QString& message) {
    if (m_message != message) {
        m_message = message;
        emit messageChanged();
    }
}

void ProfilePickerViewModel::report(const core::Error& error) {
    qCritical("ProfilePickerViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
