#include "viewmodels/ChangePinViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "services/Session.hpp"

#include <gtest/gtest.h>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::services::PinCheckStatus;
using fdss::viewmodels::ChangePinViewModel;

namespace {

const auto kCurrent = QStringLiteral("1234");
const auto kNewPin = QStringLiteral("5678");
const auto kWrongPin = QStringLiteral("9999");

class ChangePinViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        m_coach = completeSetup().id;
    }

    StaffUserId m_coach;
};

} // namespace

TEST_F(ChangePinViewModelTest, TheNewPinWorksAndTheOldOneDoesNot) {
    ChangePinViewModel change;

    EXPECT_TRUE(change.change(kCurrent, kNewPin, kNewPin));

    registry().auth().logout();
    EXPECT_EQ(registry().auth().login(m_coach, kPin).value_or(fdss::services::PinCheck{}).status,
              PinCheckStatus::WrongPin);
    EXPECT_EQ(registry()
                  .auth()
                  .login(m_coach, kNewPin.toStdString())
                  .value_or(fdss::services::PinCheck{})
                  .status,
              PinCheckStatus::Accepted);
}

TEST_F(ChangePinViewModelTest, EveryRoleMayChangeItsOwnPin) {
    const auto analyst = addStaff("Anna Analityk", Role::Analyst);
    ASSERT_TRUE(registry().auth().login(analyst, kPin).has_value());
    ChangePinViewModel change;

    EXPECT_TRUE(change.change(kCurrent, kNewPin, kNewPin));
}

TEST_F(ChangePinViewModelTest, AWrongCurrentPinNamesTheAttemptsLeft) {
    ChangePinViewModel change;

    EXPECT_FALSE(change.change(kWrongPin, kNewPin, kNewPin));

    EXPECT_EQ(change.currentPinError(), QStringLiteral("Wrong PIN. Attempts left: 4."));
}

TEST_F(ChangePinViewModelTest, RepeatedWrongPinsLockTheUserOut) {
    ChangePinViewModel change;

    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(change.change(kWrongPin, kNewPin, kNewPin));
    }

    EXPECT_EQ(change.currentPinError(), QStringLiteral("Too many wrong PINs. Try again in 30 s."));
    EXPECT_TRUE(registry().session().user().has_value());
}

TEST_F(ChangePinViewModelTest, DifferentNewPinsCostNoAttempt) {
    ChangePinViewModel change;

    EXPECT_FALSE(change.change(kWrongPin, kNewPin, QStringLiteral("0000")));

    EXPECT_FALSE(change.pinRepeatError().isEmpty());
    EXPECT_TRUE(change.currentPinError().isEmpty());
    EXPECT_FALSE(change.change(kWrongPin, kNewPin, kNewPin));
    EXPECT_EQ(change.currentPinError(), QStringLiteral("Wrong PIN. Attempts left: 4."));
}

TEST_F(ChangePinViewModelTest, AMalformedNewPinIsShownOnThatField) {
    ChangePinViewModel change;

    EXPECT_FALSE(change.change(kCurrent, QStringLiteral("12"), QStringLiteral("12")));

    EXPECT_FALSE(change.newPinError().isEmpty());
    EXPECT_TRUE(change.pinRepeatError().isEmpty());
}

TEST_F(ChangePinViewModelTest, AMalformedCurrentPinIsShownOnThatField) {
    ChangePinViewModel change;

    EXPECT_FALSE(change.change(QStringLiteral("1"), kNewPin, kNewPin));

    EXPECT_FALSE(change.currentPinError().isEmpty());
    EXPECT_TRUE(change.newPinError().isEmpty());
}

TEST_F(ChangePinViewModelTest, ClearErrorsClearsThemAll) {
    ChangePinViewModel change;
    ASSERT_FALSE(change.change(kWrongPin, kNewPin, kNewPin));

    change.clearErrors();

    EXPECT_TRUE(change.currentPinError().isEmpty());
}
