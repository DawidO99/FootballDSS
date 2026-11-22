#include "viewmodels/ChangePinViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "services/Session.hpp"

#include <QEventLoop>
#include <QTimer>

#include <gtest/gtest.h>

#include <chrono>

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

    static void lockOut(ChangePinViewModel& change) {
        for (int i = 0; i < 5; ++i) {
            EXPECT_FALSE(change.change(kWrongPin, kNewPin, kNewPin));
        }
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

TEST_F(ChangePinViewModelTest, ANewPinEqualToTheCurrentOneIsShownOnThatField) {
    ChangePinViewModel change;

    EXPECT_FALSE(change.change(kCurrent, kCurrent, kCurrent));

    EXPECT_EQ(change.newPinError(),
              QStringLiteral("The new PIN must differ from the current one."));
    EXPECT_TRUE(change.currentPinError().isEmpty());
    EXPECT_TRUE(change.pinRepeatError().isEmpty());
}

TEST_F(ChangePinViewModelTest, ANewPinEqualToTheCurrentOneDoesNotCallTheService) {
    ChangePinViewModel change;

    // Through the service this would be a wrong current PIN, and would count as an attempt.
    EXPECT_FALSE(change.change(kWrongPin, kWrongPin, kWrongPin));
    EXPECT_TRUE(change.currentPinError().isEmpty());

    EXPECT_FALSE(change.change(kWrongPin, kNewPin, kNewPin));
    EXPECT_EQ(change.currentPinError(), QStringLiteral("Wrong PIN. Attempts left: 4."));
}

TEST_F(ChangePinViewModelTest, TheLockoutCountsDownAndEnds) {
    ChangePinViewModel change;
    int lockedChanges = 0;
    QObject::connect(&change, &ChangePinViewModel::lockedChanged, &change,
                     [&] { ++lockedChanges; });
    lockOut(change);
    ASSERT_TRUE(change.locked());
    EXPECT_EQ(lockedChanges, 1);

    const SignalCount errors(&change, &ChangePinViewModel::errorsChanged);
    m_clock->advance(std::chrono::seconds{12});
    change.tickLock();
    EXPECT_TRUE(change.locked());
    EXPECT_EQ(change.currentPinError(), QStringLiteral("Too many wrong PINs. Try again in 18 s."));
    EXPECT_EQ(errors.count(), 1);

    m_clock->advance(std::chrono::seconds{18});
    change.tickLock();
    EXPECT_FALSE(change.locked());
    EXPECT_TRUE(change.currentPinError().isEmpty());
    EXPECT_EQ(lockedChanges, 2);

    EXPECT_TRUE(change.change(kCurrent, kNewPin, kNewPin));
}

TEST_F(ChangePinViewModelTest, OneTickAfterASleepLongerThanTheLockoutUnlocks) {
    ChangePinViewModel change;
    lockOut(change);

    m_clock->advance(std::chrono::seconds{31});
    change.tickLock();

    EXPECT_FALSE(change.locked());
    EXPECT_TRUE(change.currentPinError().isEmpty());
}

TEST_F(ChangePinViewModelTest, DuringALockoutTheServiceIsNotCalled) {
    ChangePinViewModel change;
    lockOut(change);
    m_clock->advance(std::chrono::seconds{10});

    // The right PIN: the service would answer with the time left, and the clock of the view has
    // not ticked yet, so the text stays as it was.
    EXPECT_FALSE(change.change(kCurrent, kNewPin, kNewPin));

    EXPECT_TRUE(change.locked());
    EXPECT_EQ(change.currentPinError(), QStringLiteral("Too many wrong PINs. Try again in 30 s."));
}

TEST_F(ChangePinViewModelTest, ClosingTheDialogDoesNotEndTheLockout) {
    ChangePinViewModel change;
    lockOut(change);

    change.clearErrors();

    EXPECT_TRUE(change.locked());
    EXPECT_EQ(change.currentPinError(), QStringLiteral("Too many wrong PINs. Try again in 30 s."));
}

TEST_F(ChangePinViewModelTest, TheTimerRunsTheCountdown) {
    ChangePinViewModel change;
    lockOut(change);
    m_clock->advance(std::chrono::seconds{1});

    QEventLoop loop;
    QObject::connect(&change, &ChangePinViewModel::errorsChanged, &loop, &QEventLoop::quit);
    QTimer::singleShot(std::chrono::seconds{5}, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(change.currentPinError(), QStringLiteral("Too many wrong PINs. Try again in 29 s."));
}

TEST_F(ChangePinViewModelTest, ClearErrorsClearsThemAll) {
    ChangePinViewModel change;
    ASSERT_FALSE(change.change(kWrongPin, kNewPin, kNewPin));

    change.clearErrors();

    EXPECT_TRUE(change.currentPinError().isEmpty());
}

// P19 decision 5.
TEST_F(ChangePinViewModelTest, TheRequiredPinNeedsNoCurrentPinAndDiffersFromTheCoachs) {
    const auto physio = addStaff("Piotr Fizjo", Role::Physio, true);
    ASSERT_TRUE(registry().auth().login(physio, kPin).has_value());
    ChangePinViewModel change;

    EXPECT_FALSE(change.setRequired(kCurrent, kCurrent));
    EXPECT_FALSE(change.newPinError().isEmpty());
    EXPECT_FALSE(change.setRequired(kNewPin, kWrongPin));
    EXPECT_FALSE(change.pinRepeatError().isEmpty());
    EXPECT_TRUE(registry().session().mustChangePin());

    EXPECT_TRUE(change.setRequired(kNewPin, kNewPin));
    EXPECT_TRUE(change.newPinError().isEmpty());
    EXPECT_FALSE(registry().session().mustChangePin());
    registry().auth().logout();
    EXPECT_EQ(registry().auth().login(physio, kNewPin.toStdString())->status,
              PinCheckStatus::Accepted);
}

TEST_F(ChangePinViewModelTest, TheRequiredPinIsRefusedWithoutTheNeedToChangeIt) {
    ChangePinViewModel change;
    const SignalCount errors(&change, &ChangePinViewModel::errorOccurred);

    EXPECT_FALSE(change.setRequired(kNewPin, kNewPin));

    EXPECT_EQ(errors.count(), 1);
}
