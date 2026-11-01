#include "viewmodels/LockCountdown.hpp"

#include "services/AuthService.hpp"
#include "services/Clock.hpp"
#include "viewmodels/ErrorPresenter.hpp"

#include <algorithm>

namespace fdss::viewmodels {

namespace {

constexpr std::chrono::seconds kTick{1};

} // namespace

LockCountdown::LockCountdown(const services::Clock& clock, QObject* parent)
    : QObject(parent), m_clock(clock) {
    m_timer.setInterval(kTick);
    connect(&m_timer, &QTimer::timeout, this, &LockCountdown::tick);
}

bool LockCountdown::locked() const { return m_left.count() > 0; }

QString LockCountdown::message() const {
    if (!locked()) {
        return {};
    }
    services::PinCheck check;
    check.status = services::PinCheckStatus::LockedOut;
    check.lockedFor = m_left;
    return ErrorPresenter::pinCheckMessage(check);
}

void LockCountdown::start(std::chrono::seconds left) {
    m_end = m_clock.now() + left;
    show(left);
}

void LockCountdown::cancel() {
    m_left = std::chrono::seconds::zero();
    m_timer.stop();
}

void LockCountdown::tick() {
    if (!locked()) {
        return;
    }
    const std::chrono::seconds left = m_end - m_clock.now();
    // More than a whole lockout left means the clock was moved back; the service treats that as
    // expired too (P07a decision 8).
    show(left > services::kLockoutDuration ? std::chrono::seconds::zero()
                                           : std::max(left, std::chrono::seconds::zero()));
}

void LockCountdown::show(std::chrono::seconds left) {
    const bool wasLocked = locked();
    m_left = left;
    if (locked()) {
        if (!m_timer.isActive()) {
            m_timer.start();
        }
    } else {
        m_timer.stop();
    }
    if (locked() != wasLocked) {
        emit lockedChanged();
    }
    if (locked() || wasLocked) {
        emit changed();
    }
}

} // namespace fdss::viewmodels
