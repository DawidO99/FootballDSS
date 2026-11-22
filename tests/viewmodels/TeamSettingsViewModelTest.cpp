#include "viewmodels/TeamSettingsViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "services/SetupService.hpp"

#include <gtest/gtest.h>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::TeamSettingsViewModel;

namespace {

class TeamSettingsViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        completeSetup();
    }
};

} // namespace

TEST_F(TeamSettingsViewModelTest, ShowsAndStoresTheNameAndSeason) {
    TeamSettingsViewModel team;
    EXPECT_TRUE(team.canEdit());
    EXPECT_EQ(team.teamName(), QStringLiteral("Seniorzy"));
    EXPECT_EQ(team.season(), QStringLiteral("2026/27"));
    const SignalCount changed(&team, &TeamSettingsViewModel::teamChanged);

    ASSERT_TRUE(team.save(QStringLiteral("  Rezerwy "), QStringLiteral(" 2027/28 ")));

    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(team.teamName(), QStringLiteral("Rezerwy"));
    EXPECT_EQ(team.season(), QStringLiteral("2027/28"));
    const auto context = registry().setup().clubContext();
    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->team.name, "Rezerwy");
    EXPECT_EQ(context->team.season, "2027/28");
    EXPECT_EQ(context->team.category, TeamCategory::Senior);
}

TEST_F(TeamSettingsViewModelTest, ABadSeasonOrNameIsShownOnItsFieldAndNothingIsStored) {
    TeamSettingsViewModel team;

    EXPECT_FALSE(team.save(QStringLiteral("Seniorzy"), QStringLiteral("2026/28")));
    EXPECT_FALSE(team.seasonError().isEmpty());
    EXPECT_TRUE(team.nameError().isEmpty());

    EXPECT_FALSE(team.save(QStringLiteral("   "), QStringLiteral("2026/27")));
    EXPECT_FALSE(team.nameError().isEmpty());
    EXPECT_TRUE(team.seasonError().isEmpty());

    team.clearErrors();
    EXPECT_TRUE(team.nameError().isEmpty());
    const auto context = registry().setup().clubContext();
    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->team.season, "2026/27");
}

TEST_F(TeamSettingsViewModelTest, OnlyARoleThatManagesSettingsMayChangeTheTeam) {
    const StaffUserId assistant = addStaff("Asystent", Role::Assistant);
    ASSERT_TRUE(registry().auth().login(assistant, kPin).has_value());
    TeamSettingsViewModel team;
    EXPECT_FALSE(team.canEdit());
    const SignalCount errors(&team, &TeamSettingsViewModel::errorOccurred);

    EXPECT_FALSE(team.save(QStringLiteral("Inna"), QStringLiteral("2027/28")));

    EXPECT_EQ(errors.count(), 1);
    EXPECT_EQ(team.teamName(), QStringLiteral("Seniorzy"));
}
