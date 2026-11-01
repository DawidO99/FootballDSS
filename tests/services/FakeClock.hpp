#pragma once

#include "services/Clock.hpp"

#include <chrono>

namespace fdss::tests {

// Starts at 2026-11-01 12:00:00 UTC and moves only when a test says so.
class FakeClock final : public services::Clock {
public:
    [[nodiscard]] core::Timestamp now() const override { return m_now; }
    [[nodiscard]] core::Date today() const override {
        return core::Date{std::chrono::floor<std::chrono::days>(m_now)};
    }

    void advance(std::chrono::seconds by) { m_now += by; }

private:
    core::Timestamp m_now =
        std::chrono::sys_days{
            core::Date{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}}} +
        std::chrono::hours{12};
};

} // namespace fdss::tests
