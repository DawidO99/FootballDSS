#include "viewmodels/SetupWizardViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "services/Session.hpp"
#include "viewmodels/AppState.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <gtest/gtest.h>

#include <chrono>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::AppState;
using fdss::viewmodels::SetupWizardViewModel;

namespace {

constexpr int kClubStep = 1;
constexpr int kCoachStep = 2;
constexpr int kSummaryStep = 3;

Date date(int year, unsigned month, unsigned day) {
    return Date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
}

class SetupWizardViewModelTest : public ViewModelTest {
protected:
    static void fillClubStep(SetupWizardViewModel& wizard) {
        wizard.setClubName(QStringLiteral("KS Łączna"));
        wizard.setTeamName(QStringLiteral("Seniorzy"));
    }

    static void fillCoachStep(SetupWizardViewModel& wizard) {
        wizard.setCoachName(QStringLiteral("Jan Trener"));
        wizard.setPin(QStringLiteral("1234"));
        wizard.setPinRepeat(QStringLiteral("1234"));
    }

    static void goToCoachStep(SetupWizardViewModel& wizard) {
        fillClubStep(wizard);
        ASSERT_TRUE(wizard.next());
        ASSERT_TRUE(wizard.next());
        ASSERT_EQ(wizard.currentStep(), kCoachStep);
    }
};

} // namespace

TEST_F(SetupWizardViewModelTest, StartsOnTheWelcomeStepWithTheSeasonOfToday) {
    const SetupWizardViewModel wizard;

    EXPECT_EQ(wizard.currentStep(), 0);
    EXPECT_EQ(wizard.stepCount(), 4);
    EXPECT_EQ(wizard.season(), QStringLiteral("2026/27")); // FakeClock: 2026-11-01
    EXPECT_EQ(wizard.category(), QStringLiteral("senior"));
}

TEST(SetupWizardDefaultSeasonTest, ANewSeasonStartsOnJulyFirst) {
    EXPECT_EQ(SetupWizardViewModel::defaultSeason(date(2027, 6, 30)), QStringLiteral("2026/27"));
    EXPECT_EQ(SetupWizardViewModel::defaultSeason(date(2027, 7, 1)), QStringLiteral("2027/28"));
    EXPECT_EQ(SetupWizardViewModel::defaultSeason(date(2027, 1, 1)), QStringLiteral("2026/27"));
    EXPECT_EQ(SetupWizardViewModel::defaultSeason(date(2026, 12, 31)), QStringLiteral("2026/27"));
}

TEST(SetupWizardDefaultSeasonTest, TheShortYearKeepsItsLeadingZero) {
    EXPECT_EQ(SetupWizardViewModel::defaultSeason(date(2008, 9, 1)), QStringLiteral("2008/09"));
    EXPECT_EQ(SetupWizardViewModel::defaultSeason(date(2099, 9, 1)), QStringLiteral("2099/00"));
}

TEST_F(SetupWizardViewModelTest, TheWelcomeStepNeedsNothing) {
    SetupWizardViewModel wizard;

    EXPECT_TRUE(wizard.next());
    EXPECT_EQ(wizard.currentStep(), kClubStep);
}

TEST_F(SetupWizardViewModelTest, EmptyNamesStayOnTheClubStep) {
    SetupWizardViewModel wizard;
    ASSERT_TRUE(wizard.next());
    wizard.setClubName(QStringLiteral("   "));

    EXPECT_FALSE(wizard.next());

    EXPECT_EQ(wizard.currentStep(), kClubStep);
    EXPECT_FALSE(wizard.clubNameError().isEmpty());
    EXPECT_FALSE(wizard.teamNameError().isEmpty());
    EXPECT_NE(wizard.clubNameError(), wizard.teamNameError());
    EXPECT_TRUE(wizard.seasonError().isEmpty());
}

TEST_F(SetupWizardViewModelTest, ANameOfOneHundredCharactersPassesAndOneMoreDoesNot) {
    SetupWizardViewModel wizard;
    ASSERT_TRUE(wizard.next());
    wizard.setTeamName(QStringLiteral("Seniorzy"));

    wizard.setClubName(QString(101, QChar(u'ł')));
    EXPECT_FALSE(wizard.next());
    EXPECT_FALSE(wizard.clubNameError().isEmpty());

    wizard.setClubName(QString(100, QChar(u'ł')));
    EXPECT_TRUE(wizard.next());
}

TEST_F(SetupWizardViewModelTest, TheSeasonFormatIsChecked) {
    SetupWizardViewModel wizard;
    ASSERT_TRUE(wizard.next());
    fillClubStep(wizard);

    for (const char* season : {"", "2026", "2026-27", "26/27", "2026/2027", "abcd/ef"}) {
        wizard.setSeason(QString::fromLatin1(season));
        EXPECT_FALSE(wizard.next()) << season;
        EXPECT_FALSE(wizard.seasonError().isEmpty()) << season;
        EXPECT_EQ(wizard.currentStep(), kClubStep) << season;
    }

    wizard.setSeason(QStringLiteral(" 2027/28 "));
    EXPECT_TRUE(wizard.next());
}

TEST_F(SetupWizardViewModelTest, AnEmptyCoachNameStaysOnTheCoachStep) {
    SetupWizardViewModel wizard;
    goToCoachStep(wizard);
    wizard.setPin(QStringLiteral("1234"));
    wizard.setPinRepeat(QStringLiteral("1234"));

    EXPECT_FALSE(wizard.next());

    EXPECT_EQ(wizard.currentStep(), kCoachStep);
    EXPECT_FALSE(wizard.coachNameError().isEmpty());
    EXPECT_TRUE(wizard.pinError().isEmpty());
}

TEST_F(SetupWizardViewModelTest, DifferentPinsStayOnTheCoachStep) {
    SetupWizardViewModel wizard;
    goToCoachStep(wizard);
    wizard.setCoachName(QStringLiteral("Jan Trener"));
    wizard.setPin(QStringLiteral("1234"));
    wizard.setPinRepeat(QStringLiteral("1235"));

    EXPECT_FALSE(wizard.next());

    EXPECT_EQ(wizard.currentStep(), kCoachStep);
    EXPECT_TRUE(wizard.pinError().isEmpty());
    EXPECT_FALSE(wizard.pinRepeatError().isEmpty());
}

TEST_F(SetupWizardViewModelTest, AMalformedPinStaysOnTheCoachStep) {
    SetupWizardViewModel wizard;
    goToCoachStep(wizard);
    wizard.setCoachName(QStringLiteral("Jan Trener"));

    for (const char* pin : {"", "123", "123456789", "12a4"}) {
        wizard.setPin(QString::fromLatin1(pin));
        wizard.setPinRepeat(QString::fromLatin1(pin));
        EXPECT_FALSE(wizard.next()) << pin;
        EXPECT_FALSE(wizard.pinError().isEmpty()) << pin;
    }
}

TEST_F(SetupWizardViewModelTest, EditingAFieldClearsItsError) {
    SetupWizardViewModel wizard;
    ASSERT_TRUE(wizard.next());
    ASSERT_FALSE(wizard.next());
    ASSERT_FALSE(wizard.clubNameError().isEmpty());

    wizard.setClubName(QStringLiteral("K"));

    EXPECT_TRUE(wizard.clubNameError().isEmpty());
    EXPECT_FALSE(wizard.teamNameError().isEmpty());
}

TEST_F(SetupWizardViewModelTest, BackKeepsTheValuesAndClearsTheErrors) {
    SetupWizardViewModel wizard;
    goToCoachStep(wizard);
    wizard.setPin(QStringLiteral("12"));
    ASSERT_FALSE(wizard.next());

    wizard.back();

    EXPECT_EQ(wizard.currentStep(), kClubStep);
    EXPECT_EQ(wizard.clubName(), QStringLiteral("KS Łączna"));
    EXPECT_EQ(wizard.pin(), QStringLiteral("12"));
    EXPECT_TRUE(wizard.pinError().isEmpty());
    EXPECT_TRUE(wizard.coachNameError().isEmpty());
}

TEST_F(SetupWizardViewModelTest, ErrorsAreMadeAnewForANewLanguage) {
    SetupWizardViewModel wizard;
    ASSERT_TRUE(wizard.next());
    ASSERT_FALSE(wizard.next());
    wizard.setClubName(QStringLiteral("KS Łączna"));
    const SignalCount errors(&wizard, &SetupWizardViewModel::errorsChanged);

    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &event);

    EXPECT_EQ(errors.count(), 1);
    EXPECT_TRUE(wizard.clubNameError().isEmpty()) << "what the user fixed stays fixed";
    EXPECT_FALSE(wizard.teamNameError().isEmpty());
    EXPECT_EQ(wizard.currentStep(), kClubStep);
}

TEST_F(SetupWizardViewModelTest, ALanguageChangeDoesNotBringUpErrorsOnAFreshStep) {
    SetupWizardViewModel wizard;
    ASSERT_TRUE(wizard.next());

    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &event);

    EXPECT_TRUE(wizard.clubNameError().isEmpty());
    EXPECT_TRUE(wizard.teamNameError().isEmpty());
}

TEST_F(SetupWizardViewModelTest, BackOnTheFirstStepStaysThere) {
    SetupWizardViewModel wizard;

    wizard.back();

    EXPECT_EQ(wizard.currentStep(), 0);
}

TEST_F(SetupWizardViewModelTest, CreateGoesFromSetupToReady) {
    AppState state;
    ASSERT_EQ(state.state(), AppState::State::NeedsSetup);
    SetupWizardViewModel wizard;
    int completed = 0;
    QObject::connect(&wizard, &SetupWizardViewModel::completed, &wizard, [&] { ++completed; });
    goToCoachStep(wizard);
    fillCoachStep(wizard);
    ASSERT_TRUE(wizard.next());
    ASSERT_EQ(wizard.currentStep(), kSummaryStep);
    wizard.setCategory(QStringLiteral("u19"));

    EXPECT_TRUE(wizard.create());

    EXPECT_EQ(completed, 1);
    state.refresh();
    EXPECT_EQ(state.state(), AppState::State::Ready);
    EXPECT_EQ(state.clubName(), QStringLiteral("KS Łączna"));
    EXPECT_EQ(state.userName(), QStringLiteral("Jan Trener"));
    EXPECT_TRUE(state.can(QStringLiteral("ManageStaff")));
    const auto context = registry().setup().clubContext();
    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->team.category, TeamCategory::U19);
    EXPECT_EQ(context->team.season, "2026/27");
}

TEST_F(SetupWizardViewModelTest, CreateForgetsThePin) {
    SetupWizardViewModel wizard;
    fillClubStep(wizard);
    fillCoachStep(wizard);

    ASSERT_TRUE(wizard.create());

    EXPECT_TRUE(wizard.pin().isEmpty());
    EXPECT_TRUE(wizard.pinRepeat().isEmpty());
}

TEST_F(SetupWizardViewModelTest, TheCoachCanLogInWithThePinOfTheWizard) {
    SetupWizardViewModel wizard;
    fillClubStep(wizard);
    fillCoachStep(wizard);
    ASSERT_TRUE(wizard.create());
    const auto coach = registry().session().user();
    ASSERT_TRUE(coach.has_value());
    registry().auth().logout();

    const auto check = registry().auth().login(coach->id, "1234");

    ASSERT_TRUE(check.has_value());
    EXPECT_EQ(check->status, fdss::services::PinCheckStatus::Accepted);
}

TEST_F(SetupWizardViewModelTest, CreateGoesBackToTheFirstStepThatIsNotValid) {
    SetupWizardViewModel wizard;
    fillClubStep(wizard);
    wizard.setCoachName(QStringLiteral("Jan Trener"));
    wizard.setPin(QStringLiteral("1"));
    int completed = 0;
    QObject::connect(&wizard, &SetupWizardViewModel::completed, &wizard, [&] { ++completed; });

    EXPECT_FALSE(wizard.create());

    EXPECT_EQ(wizard.currentStep(), kCoachStep);
    EXPECT_FALSE(wizard.pinError().isEmpty());
    EXPECT_EQ(completed, 0);
    EXPECT_FALSE(registry().session().user().has_value());
}

TEST_F(SetupWizardViewModelTest, ASecondSetupIsReportedAndIsNoCompletion) {
    completeSetup();
    SetupWizardViewModel wizard;
    fillClubStep(wizard);
    fillCoachStep(wizard);
    QString message;
    int completed = 0;
    QObject::connect(&wizard, &SetupWizardViewModel::errorOccurred, &wizard,
                     [&](const QString& text) { message = text; });
    QObject::connect(&wizard, &SetupWizardViewModel::completed, &wizard, [&] { ++completed; });

    EXPECT_FALSE(wizard.create());

    EXPECT_FALSE(message.isEmpty());
    EXPECT_EQ(completed, 0);
    EXPECT_EQ(wizard.pin(), QStringLiteral("1234"));
}
