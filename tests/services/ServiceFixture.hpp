#pragma once

// A migrated in-memory database with an AuthService on a fake clock, for the service tests.

#include "../data/RepositoryFixture.hpp"
#include "FakeClock.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/util/Error.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "services/AuthService.hpp"
#include "services/Session.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace fdss::tests {

template <class T>
void expectError(const core::Result<T>& result, core::ErrorCode code, std::string_view field = {}) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, code) << result.error().detail;
    EXPECT_EQ(result.error().field, field);
}

class ServiceTest : public RepositoryTest {
protected:
    // Enough to run PBKDF2 through; the real count would only make the tests slow.
    static constexpr int kIterations = 4;
    static constexpr const char* kPin = "1234";

    void SetUp() override {
        RepositoryTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        m_staff = std::make_unique<data::StaffRepository>(*m_db);
        m_auth = std::make_unique<services::AuthService>(*m_db, *m_staff, m_session, m_clock,
                                                         kIterations);
    }

    static core::StaffUserDraft draft(const char* name, core::Role role, bool active = true) {
        core::StaffUserDraft result;
        result.name = name;
        result.role = role;
        result.active = active;
        return result;
    }

    // Straight through the repository: no session needed.
    core::StaffUserId addUser(const char* name, core::Role role, bool active = true,
                              const char* pin = kPin) {
        const auto hash = m_auth->hashPin(pin);
        EXPECT_TRUE(hash.has_value());
        const auto id = m_staff->create(draft(name, role, active), hash.value_or(core::PinHash{}));
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(core::StaffUserId{});
    }

    void logIn(core::StaffUserId id, const char* pin = kPin) {
        const auto check = m_auth->login(id, pin);
        ASSERT_TRUE(check.has_value()) << check.error().detail;
        ASSERT_EQ(check->status, services::PinCheckStatus::Accepted);
    }

    [[nodiscard]] core::StaffCredentials credentials(core::StaffUserId id) const {
        const auto result = m_staff->credentials(id);
        EXPECT_TRUE(result.has_value()) << result.error().detail;
        return result.value_or(core::StaffCredentials{});
    }

    [[nodiscard]] core::StaffUser stored(core::StaffUserId id) const {
        const auto result = m_staff->get(id);
        EXPECT_TRUE(result.has_value()) << result.error().detail;
        return result.value_or(core::StaffUser{});
    }

    FakeClock m_clock;
    services::Session m_session;
    std::unique_ptr<data::StaffRepository> m_staff;
    std::unique_ptr<services::AuthService> m_auth;
};

} // namespace fdss::tests
