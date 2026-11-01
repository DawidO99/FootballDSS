#pragma once

#include "core/domain/DateTypes.hpp"

#include <QDate>
#include <QDateTime>

#include <chrono>

namespace fdss::services {

// Where the services get "now" and "today" from, so tests can move time. Core never asks for them.
class Clock {
public:
    Clock() = default;
    virtual ~Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;

    [[nodiscard]] virtual core::Timestamp now() const = 0; // UTC
    [[nodiscard]] virtual core::Date today() const = 0;    // local calendar date
};

class SystemClock final : public Clock {
public:
    [[nodiscard]] core::Timestamp now() const override {
        return core::Timestamp{std::chrono::seconds{QDateTime::currentSecsSinceEpoch()}};
    }

    [[nodiscard]] core::Date today() const override {
        const QDate date = QDate::currentDate();
        return core::Date{std::chrono::year{date.year()},
                          std::chrono::month{static_cast<unsigned>(date.month())},
                          std::chrono::day{static_cast<unsigned>(date.day())}};
    }
};

} // namespace fdss::services
