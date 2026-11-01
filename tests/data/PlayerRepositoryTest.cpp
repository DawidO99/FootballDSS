#include "data/repositories/PlayerRepository.hpp"

#include "RepositoryFixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

constexpr Date kArchiveDate = 2026y / November / 1d;

class PlayerRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedTeams();
    }

    [[nodiscard]] PlayerRepository repository() { return PlayerRepository(*m_db); }

    static PlayerDraft draft(std::optional<int> shirtNumber = std::nullopt) {
        PlayerDraft result;
        result.firstName = "Jan";
        result.lastName = "Kowalski";
        result.shirtNumber = shirtNumber;
        return result;
    }

    PlayerId create(std::optional<int> shirtNumber = std::nullopt, TeamId team = kTeam) {
        const auto id = repository().create(team, draft(shirtNumber));
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(PlayerId{});
    }

    void expectShirtConflict(const Status& status) {
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, ErrorCode::Conflict) << status.error().detail;
        EXPECT_EQ(status.error().field, "shirt_number") << status.error().detail;
    }
};

} // namespace

TEST_F(PlayerRepositoryTest, CreatedPlayerKeepsEveryField) {
    PlayerDraft full;
    full.firstName = "  Łukasz ";
    full.lastName = " Żółć\t";
    full.shirtNumber = 10;
    full.birthYear = 1999;
    full.primaryPosition = Position::Am;
    full.secondaryPositions = {Position::Cm, Position::Rw};
    full.foot = PreferredFoot::Left;
    full.notes = "  kapitan ";
    const auto id = repository().create(kTeam, full);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto player = repository().get(*id);
    ASSERT_TRUE(player.has_value()) << player.error().detail;
    EXPECT_EQ(player->id, *id);
    EXPECT_EQ(player->teamId, kTeam);
    EXPECT_EQ(player->firstName, "Łukasz");
    EXPECT_EQ(player->lastName, "Żółć");
    EXPECT_EQ(player->shirtNumber, 10);
    EXPECT_EQ(player->birthYear, 1999);
    EXPECT_EQ(player->primaryPosition, Position::Am);
    EXPECT_EQ(player->secondaryPositions, (std::vector{Position::Cm, Position::Rw}));
    EXPECT_EQ(player->foot, PreferredFoot::Left);
    EXPECT_EQ(player->notes, "kapitan");
    EXPECT_EQ(player->archivedAt, std::nullopt);
}

TEST_F(PlayerRepositoryTest, OptionalFieldsStayEmpty) {
    PlayerDraft minimal = draft();
    minimal.notes = "   ";
    const auto id = repository().create(kTeam, minimal);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto player = repository().get(*id);
    ASSERT_TRUE(player.has_value());
    EXPECT_EQ(player->shirtNumber, std::nullopt);
    EXPECT_EQ(player->birthYear, std::nullopt);
    EXPECT_TRUE(player->secondaryPositions.empty());
    EXPECT_EQ(player->foot, PreferredFoot::Unknown);
    EXPECT_EQ(player->notes, std::nullopt);
}

TEST_F(PlayerRepositoryTest, GetOfAMissingPlayerIsNotFound) {
    const auto player = repository().get(kNoPlayer);
    ASSERT_FALSE(player.has_value());
    EXPECT_EQ(player.error().code, ErrorCode::NotFound);
}

TEST_F(PlayerRepositoryTest, PlayerOfAMissingTeamIsInvalidInput) {
    const auto id = repository().create(kNoTeam, draft());
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(PlayerRepositoryTest, OutOfRangeShirtNumberIsInvalidInput) {
    const auto id = repository().create(kTeam, draft(100));
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(PlayerRepositoryTest, TooLongNameIsInvalidInput) {
    PlayerDraft tooLong = draft();
    tooLong.lastName = std::string(51, 'x');
    const auto id = repository().create(kTeam, tooLong);
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(PlayerRepositoryTest, ListReturnsOnlyPlayersOfTheTeam) {
    const PlayerId first = create(7);
    const PlayerId second = create(8);
    create(7, kOtherTeam);

    const auto players = repository().list(kTeam, ArchivedPlayers::Exclude);
    ASSERT_TRUE(players.has_value()) << players.error().detail;
    ASSERT_EQ(players->size(), 2U);
    EXPECT_EQ(players->at(0).id, first);
    EXPECT_EQ(players->at(1).id, second);
}

TEST_F(PlayerRepositoryTest, ListHidesArchivedPlayersUnlessAsked) {
    const PlayerId active = create(7);
    const PlayerId archived = create(8);
    ASSERT_TRUE(repository().archive(archived, kArchiveDate).has_value());

    const auto withoutArchived = repository().list(kTeam, ArchivedPlayers::Exclude);
    ASSERT_TRUE(withoutArchived.has_value());
    ASSERT_EQ(withoutArchived->size(), 1U);
    EXPECT_EQ(withoutArchived->at(0).id, active);

    const auto withArchived = repository().list(kTeam, ArchivedPlayers::Include);
    ASSERT_TRUE(withArchived.has_value());
    ASSERT_EQ(withArchived->size(), 2U);
    EXPECT_EQ(withArchived->at(1).archivedAt, kArchiveDate);
}

TEST_F(PlayerRepositoryTest, UpdateChangesEveryFieldAndClearsOptionalOnes) {
    const PlayerId id = create(7);
    PlayerDraft changed = draft(9);
    changed.firstName = "Piotr";
    changed.birthYear = 2001;
    changed.primaryPosition = Position::Gk;
    changed.secondaryPositions = {Position::Cb};
    changed.foot = PreferredFoot::Both;
    changed.notes = "uwaga";
    ASSERT_TRUE(repository().update(id, changed).has_value());

    const auto player = repository().get(id);
    ASSERT_TRUE(player.has_value());
    EXPECT_EQ(player->firstName, "Piotr");
    EXPECT_EQ(player->shirtNumber, 9);
    EXPECT_EQ(player->birthYear, 2001);
    EXPECT_EQ(player->primaryPosition, Position::Gk);
    EXPECT_EQ(player->secondaryPositions, std::vector{Position::Cb});
    EXPECT_EQ(player->foot, PreferredFoot::Both);
    EXPECT_EQ(player->notes, "uwaga");

    ASSERT_TRUE(repository().update(id, draft()).has_value());
    const auto cleared = repository().get(id);
    ASSERT_TRUE(cleared.has_value());
    EXPECT_EQ(cleared->shirtNumber, std::nullopt);
    EXPECT_EQ(cleared->birthYear, std::nullopt);
    EXPECT_TRUE(cleared->secondaryPositions.empty());
    EXPECT_EQ(cleared->notes, std::nullopt);
}

TEST_F(PlayerRepositoryTest, UpdateOfAMissingPlayerIsNotFound) {
    const auto status = repository().update(kNoPlayer, draft());
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
}

// --- Shirt numbers ---

TEST_F(PlayerRepositoryTest, DuplicateShirtNumberOnCreateIsAConflict) {
    create(7);
    const auto id = repository().create(kTeam, draft(7));
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::Conflict) << id.error().detail;
    EXPECT_EQ(id.error().field, "shirt_number") << id.error().detail;
}

TEST_F(PlayerRepositoryTest, DuplicateShirtNumberOnUpdateIsAConflict) {
    create(7);
    const PlayerId other = create(8);
    expectShirtConflict(repository().update(other, draft(7)));
}

TEST_F(PlayerRepositoryTest, SameShirtNumberIsFineInAnotherTeam) {
    create(7);
    EXPECT_TRUE(repository().create(kOtherTeam, draft(7)).has_value());
}

TEST_F(PlayerRepositoryTest, UpdateKeepingTheOwnShirtNumberIsFine) {
    const PlayerId id = create(7);
    EXPECT_TRUE(repository().update(id, draft(7)).has_value());
}

TEST_F(PlayerRepositoryTest, ArchivedPlayerFreesTheShirtNumber) {
    const PlayerId archived = create(7);
    ASSERT_TRUE(repository().archive(archived, kArchiveDate).has_value());
    EXPECT_TRUE(repository().create(kTeam, draft(7)).has_value());
}

TEST_F(PlayerRepositoryTest, UnarchiveIsAConflictWhenTheNumberWasTaken) {
    const PlayerId archived = create(7);
    ASSERT_TRUE(repository().archive(archived, kArchiveDate).has_value());
    create(7);
    expectShirtConflict(repository().unarchive(archived));

    const auto player = repository().get(archived);
    ASSERT_TRUE(player.has_value());
    EXPECT_EQ(player->archivedAt, kArchiveDate);
}

// --- Archiving ---

TEST_F(PlayerRepositoryTest, ArchiveAndUnarchiveRoundTrip) {
    const PlayerId id = create(7);
    ASSERT_TRUE(repository().archive(id, kArchiveDate).has_value());
    EXPECT_EQ(repository().get(id)->archivedAt, kArchiveDate);

    ASSERT_TRUE(repository().unarchive(id).has_value());
    EXPECT_EQ(repository().get(id)->archivedAt, std::nullopt);
}

TEST_F(PlayerRepositoryTest, ArchivingTwiceKeepsTheFirstDate) {
    const PlayerId id = create();
    ASSERT_TRUE(repository().archive(id, kArchiveDate).has_value());
    ASSERT_TRUE(repository().archive(id, 2027y / January / 1d).has_value());
    EXPECT_EQ(repository().get(id)->archivedAt, kArchiveDate);
}

TEST_F(PlayerRepositoryTest, ArchiveAndUnarchiveOfAMissingPlayerAreNotFound) {
    const auto archived = repository().archive(kNoPlayer, kArchiveDate);
    ASSERT_FALSE(archived.has_value());
    EXPECT_EQ(archived.error().code, ErrorCode::NotFound);

    const auto unarchived = repository().unarchive(kNoPlayer);
    ASSERT_FALSE(unarchived.has_value());
    EXPECT_EQ(unarchived.error().code, ErrorCode::NotFound);
}

TEST_F(PlayerRepositoryTest, UnknownStoredPositionFailsTheRead) {
    const PlayerId id = create();
    ASSERT_TRUE(sql("UPDATE player SET secondary_positions = 'xx' WHERE id = ?",
                    {static_cast<qlonglong>(id.value)}));
    const auto player = repository().get(id);
    ASSERT_FALSE(player.has_value());
    EXPECT_EQ(player.error().code, ErrorCode::Database);
}
