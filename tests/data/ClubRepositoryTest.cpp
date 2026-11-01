#include "data/repositories/ClubRepository.hpp"

#include "RepositoryFixture.hpp"

#include <gtest/gtest.h>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;

namespace {

class ClubRepositoryTest : public RepositoryTest {
protected:
    [[nodiscard]] ClubRepository repository() { return ClubRepository(*m_db); }

    static TeamDraft teamDraft(const char* name = "Seniorzy") {
        TeamDraft draft;
        draft.name = name;
        draft.category = TeamCategory::U19;
        draft.season = "2026/27";
        return draft;
    }
};

} // namespace

TEST_F(ClubRepositoryTest, GetClubWithoutAClubIsNotFound) {
    const auto club = repository().getClub();
    ASSERT_FALSE(club.has_value());
    EXPECT_EQ(club.error().code, ErrorCode::NotFound);
}

TEST_F(ClubRepositoryTest, CreatedClubCanBeRead) {
    const auto id = repository().createClub(ClubDraft{"  KS Śląsk  "});
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto club = repository().getClub();
    ASSERT_TRUE(club.has_value()) << club.error().detail;
    EXPECT_EQ(club->id, *id);
    EXPECT_EQ(club->name, "KS Śląsk");
}

TEST_F(ClubRepositoryTest, SecondClubIsAConflictAndChangesNothing) {
    ASSERT_TRUE(repository().createClub(ClubDraft{"KS Test"}).has_value());
    const auto second = repository().createClub(ClubDraft{"Inny"});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::Conflict);
    EXPECT_EQ(count("SELECT count(*) FROM club"), 1);
    EXPECT_EQ(repository().getClub()->name, "KS Test");
}

TEST_F(ClubRepositoryTest, EmptyClubNameIsInvalidInput) {
    const auto id = repository().createClub(ClubDraft{"   "});
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(count("SELECT count(*) FROM club"), 0);
}

TEST_F(ClubRepositoryTest, TeamsOfAClubAreListed) {
    const ClubId club = repository().createClub(ClubDraft{"KS Test"}).value();
    const auto first = repository().createTeam(club, teamDraft(" Seniorzy "));
    const auto second = repository().createTeam(club, teamDraft("Juniorzy"));
    ASSERT_TRUE(first.has_value()) << first.error().detail;
    ASSERT_TRUE(second.has_value()) << second.error().detail;

    const auto teams = repository().listTeams(club);
    ASSERT_TRUE(teams.has_value()) << teams.error().detail;
    ASSERT_EQ(teams->size(), 2U);
    EXPECT_EQ(teams->at(0).id, *first);
    EXPECT_EQ(teams->at(0).clubId, club);
    EXPECT_EQ(teams->at(0).name, "Seniorzy");
    EXPECT_EQ(teams->at(0).category, TeamCategory::U19);
    EXPECT_EQ(teams->at(0).season, "2026/27");
    EXPECT_EQ(teams->at(1).id, *second);
}

TEST_F(ClubRepositoryTest, ListTeamsOfAnotherClubIsEmpty) {
    seedTeams();
    const auto teams = repository().listTeams(ClubId{999});
    ASSERT_TRUE(teams.has_value());
    EXPECT_TRUE(teams->empty());
}

TEST_F(ClubRepositoryTest, TeamOfAMissingClubIsInvalidInput) {
    const auto id = repository().createTeam(ClubId{999}, teamDraft());
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(ClubRepositoryTest, BadSeasonIsInvalidInput) {
    seedTeams();
    TeamDraft draft = teamDraft();
    draft.season = "2026";
    const auto id = repository().createTeam(kClub, draft);
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(ClubRepositoryTest, UpdateTeamChangesItsFields) {
    seedTeams();
    TeamDraft draft = teamDraft("Rezerwy");
    draft.category = TeamCategory::Other;
    draft.season = "2027/28";
    ASSERT_TRUE(repository().updateTeam(kTeam, draft).has_value());

    const auto teams = repository().listTeams(kClub);
    ASSERT_TRUE(teams.has_value());
    EXPECT_EQ(teams->at(0).name, "Rezerwy");
    EXPECT_EQ(teams->at(0).category, TeamCategory::Other);
    EXPECT_EQ(teams->at(0).season, "2027/28");
    EXPECT_EQ(teams->at(1).name, "Juniorzy");
}

TEST_F(ClubRepositoryTest, UpdateOfAMissingTeamIsNotFound) {
    const auto status = repository().updateTeam(kNoTeam, teamDraft());
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
}
