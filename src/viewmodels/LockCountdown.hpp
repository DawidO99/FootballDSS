#pragma once

#include "core/domain/DateTypes.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>

namespace fdss::services {
class Clock;
} // namespace fdss::services

namespace fdss::viewmodels {

// The time left of a PIN lockout, for the viewmodels that show one. Counts down from the end time
// with no database, so a read that fails cannot leave a field locked after the time is up, and the
// first tick after the computer sleeps shows the true time.
class LockCountdown : public QObject {
    Q_OBJECT

public:
    explicit LockCountdown(const services::Clock& clock, QObject* parent = nullptr);

    [[nodiscard]] bool locked() const;
    // The lockout with its time left, translated when called. Empty when not locked.
    [[nodiscard]] QString message() const;

    // The answer of the service: this much time is left, zero when not locked.
    void start(std::chrono::seconds left);
    // Forgets the lockout with no signal, when it is about another user from now on.
    void cancel();
    // The timer calls it every second; public for the tests of the viewmodels.
    void tick();

signals:
    // On every tick of a lockout and at its end.
    void changed();
    void lockedChanged();

private:
    void show(std::chrono::seconds left);

    const services::Clock& m_clock;
    QTimer m_timer;
    core::Timestamp m_end;
    std::chrono::seconds m_left{0};
};

} // namespace fdss::viewmodels
