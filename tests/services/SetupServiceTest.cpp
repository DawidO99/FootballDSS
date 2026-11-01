#include "services/SetupService.hpp"

#include "ServiceFixture.hpp"
#include "data/repositories/ClubRepository.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

class SetupServiceTest : public ServiceTest {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        m_clubs = std::make_unique<ClubRepository>(*m_db);
        m_setup = std::make_unique<SetupService>(*m_db, *m_clubs, *m_staff, *m_auth, m_session);
    }

    static ClubDraft club(const char* name = "KS Test") {
        ClubDraft result;
        result.name = name;
        return result;
    }

    static TeamDraft team(const char* name = "Seniorzy", const char* season = "2026/27") {
        TeamDraft result;
        result.name = name;
        result.category = TeamCategory::Senior;
        result.season = season;
        return result;
    }

    [[nodiscard]] Result<StaffUser> setup() {
        return m_setup->completeSetup(club(), team(), "Jan Trener", kPin);
    }

    void expectNothingStored() const {
        EXPECT_EQ(count("SELECT count(*) FROM club"), 0);
        EXPECT_EQ(count("SELECT count(*) FROM team"), 0);
        EXPECT_EQ(count("SELECT count(*) FROM staff_user"), 0);
        EXPECT_FALSE(m_session.user().has_value());
        EXPECT_FALSE(m_setup->isSetupComplete().value_or(true));
    }

    std::unique_ptr<ClubRepository> m_clubs;
    std::unique_ptr<SetupService> m_setup;
};

} // namespace

TEST_F(SetupServiceTest, EmptyDatabaseIsNotSetUp) {
    const auto complete = m_setup->isSetupComplete();

    ASSERT_TRUE(complete.has_value()) << complete.error().detail;
    EXPECT_FALSE(*complete);
    expectError(m_setup->clubContext(), ErrorCode::NotFound);
}

TEST_F(SetupServiceTest, CompleteSetupStoresEverythingAndLogsTheCoachIn) {
    const auto user = m_setup->completeSetup(club("  KS Test "), team(), "  Jan Trener ", kPin);

    ASSERT_TRUE(user.has_value()) << user.error().detail;
    EXPECT_EQ(user->name, "Jan Trener");
    EXPECT_EQ(user->role, Role::Coach);
    EXPECT_TRUE(user->active);
    ASSERT_TRUE(m_session.user().has_value());
    EXPECT_EQ(m_session.user()->id, user->id);
    EXPECT_TRUE(m_session.isAllowed(Permission::ManageStaff));
    EXPECT_TRUE(m_setup->isSetupComplete().value_or(false));

    const auto context = m_setup->clubContext();
    ASSERT_TRUE(context.has_value()) << context.error().detail;
    EXPECT_EQ(context->club.name, "KS Test");
    EXPECT_EQ(context->team.name, "Seniorzy");
    EXPECT_EQ(context->team.season, "2026/27");
    EXPECT_EQ(context->team.clubId, context->club.id);
}

TEST_F(SetupServiceTest, TheCoachCanLogInAgainWithThePin) {
    const auto user = setup();
    ASSERT_TRUE(user.has_value()) << user.error().detail;

    m_auth->logout();
    logIn(user->id);

    EXPECT_EQ(m_session.user()->id, user->id);
}

TEST_F(SetupServiceTest, ClubContextIsTheFirstTeam) {
    ASSERT_TRUE(setup().has_value());
    const auto first = m_setup->clubContext();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(m_clubs->createTeam(first->club.id, team("Juniorzy")).has_value());

    const auto context = m_setup->clubContext();

    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->team.name, "Seniorzy");
}

TEST_F(SetupServiceTest, FailureOfTheCoachLeavesNoClubAndNoTeam) {
    ASSERT_TRUE(sql("CREATE TRIGGER forced_failure BEFORE INSERT ON staff_user "
                    "BEGIN SELECT RAISE(ABORT, 'forced failure'); END"));

    EXPECT_FALSE(setup().has_value());

    expectNothingStored();
}

TEST_F(SetupServiceTest, FailureOfTheTeamLeavesNoClub) {
    ASSERT_TRUE(sql("CREATE TRIGGER forced_failure BEFORE INSERT ON team "
                    "BEGIN SELECT RAISE(ABORT, 'forced failure'); END"));

    EXPECT_FALSE(setup().has_value());

    expectNothingStored();
}

TEST_F(SetupServiceTest, SetupCanBeRepeatedAfterAFailure) {
    ASSERT_TRUE(sql("CREATE TRIGGER forced_failure BEFORE INSERT ON staff_user "
                    "BEGIN SELECT RAISE(ABORT, 'forced failure'); END"));
    ASSERT_FALSE(setup().has_value());
    ASSERT_TRUE(sql("DROP TRIGGER forced_failure"));

    const auto user = setup();

    ASSERT_TRUE(user.has_value()) << user.error().detail;
    EXPECT_TRUE(m_setup->isSetupComplete().value_or(false));
}

TEST_F(SetupServiceTest, ValidationNamesTheInput) {
    expectError(m_setup->completeSetup(club(" "), team(), "Jan", kPin), ErrorCode::InvalidInput,
                "club_name");
    expectError(m_setup->completeSetup(club(), team(" "), "Jan", kPin), ErrorCode::InvalidInput,
                "team_name");
    expectError(m_setup->completeSetup(club(), team("Seniorzy", "2026"), "Jan", kPin),
                ErrorCode::InvalidInput, "season");
    expectError(m_setup->completeSetup(club(), team(), " ", kPin), ErrorCode::InvalidInput,
                "coach_name");
    expectError(m_setup->completeSetup(club(), team(), "Jan", "12"), ErrorCode::InvalidInput,
                "pin");

    expectNothingStored();
}

TEST_F(SetupServiceTest, SecondSetupIsAConflictAndChangesNothing) {
    ASSERT_TRUE(setup().has_value());
    m_auth->logout();

    expectError(m_setup->completeSetup(club("Inny"), team("Inna"), "Ewa", kPin),
                ErrorCode::Conflict);

    EXPECT_EQ(count("SELECT count(*) FROM club"), 1);
    EXPECT_EQ(count("SELECT count(*) FROM team"), 1);
    EXPECT_EQ(count("SELECT count(*) FROM staff_user"), 1);
    EXPECT_FALSE(m_session.user().has_value());
}

TEST_F(SetupServiceTest, ADatabaseWithoutAnActiveCoachIsNotSetUp) {
    seedTeams();
    EXPECT_FALSE(m_setup->isSetupComplete().value_or(true));

    addUser("Ewa", Role::Physio);
    EXPECT_FALSE(m_setup->isSetupComplete().value_or(true));

    addUser("Stary", Role::Coach, false);
    EXPECT_FALSE(m_setup->isSetupComplete().value_or(true));

    addUser("Jan", Role::Coach);
    EXPECT_TRUE(m_setup->isSetupComplete().value_or(false));
}

TEST_F(SetupServiceTest, AClubWithoutATeamIsNotSetUp) {
    ASSERT_TRUE(sql("INSERT INTO club (id, name) VALUES (1, 'KS Test')"));
    addUser("Jan", Role::Coach);

    EXPECT_FALSE(m_setup->isSetupComplete().value_or(true));
    expectError(m_setup->clubContext(), ErrorCode::NotFound);
}
