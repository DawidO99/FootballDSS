#include "viewmodels/ErrorPresenter.hpp"

#include <gtest/gtest.h>

#include <array>

using namespace fdss::core;
using fdss::viewmodels::ErrorPresenter;

namespace {

constexpr std::array kAllCodes{ErrorCode::NotFound,     ErrorCode::InvalidInput,
                               ErrorCode::Conflict,     ErrorCode::PermissionDenied,
                               ErrorCode::Database,     ErrorCode::DatabaseLocked,
                               ErrorCode::NotADatabase, ErrorCode::IncompatibleVersion,
                               ErrorCode::Io,           ErrorCode::UnsupportedFormat,
                               ErrorCode::Internal};

constexpr auto kDetail = "technical detail for the log";

QString message(ErrorCode code, const char* field = "") {
    return ErrorPresenter::message(Error{code, kDetail, field});
}

} // namespace

TEST(ErrorPresenterTest, EveryCodeHasATextWithoutTheDetail) {
    for (const ErrorCode code : kAllCodes) {
        const QString text = message(code);
        EXPECT_FALSE(text.isEmpty()) << toString(code);
        EXPECT_FALSE(text.contains(QLatin1StringView(kDetail))) << toString(code);
    }
}

TEST(ErrorPresenterTest, EveryCodeHasAStartupText) {
    for (const ErrorCode code : kAllCodes) {
        EXPECT_FALSE(ErrorPresenter::startupMessage(code).isEmpty()) << toString(code);
    }
}

TEST(ErrorPresenterTest, EveryFieldOfAuthAndSetupHasItsOwnText) {
    const QString generic = message(ErrorCode::InvalidInput);
    for (const char* field : {"pin", "club_name", "team_name", "coach_name", "name", "season"}) {
        EXPECT_NE(message(ErrorCode::InvalidInput, field), generic) << field;
    }
}

TEST(ErrorPresenterTest, TheLastCoachHasItsOwnText) {
    EXPECT_NE(message(ErrorCode::Conflict, "last_coach"), message(ErrorCode::Conflict));
}

TEST(ErrorPresenterTest, AnUnknownFieldNeverShowsTheColumnName) {
    const QString invalid = message(ErrorCode::InvalidInput, "secret_column");
    const QString conflict = message(ErrorCode::Conflict, "secret_column");

    EXPECT_EQ(invalid, message(ErrorCode::InvalidInput));
    EXPECT_EQ(conflict, message(ErrorCode::Conflict));
    EXPECT_FALSE(invalid.contains(QLatin1StringView("secret_column")));
    EXPECT_FALSE(conflict.contains(QLatin1StringView("secret_column")));
}
