#include "data/repositories/AvailabilityRepository.hpp"

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

constexpr Date kFrom = 2026y / November / 10d;
constexpr Date kDayBefore = 2026y / November / 9d;
constexpr Date kExpected = 2026y / November / 20d;
constexpr Date kDayAfterExpected = 2026y / November / 21d;
constexpr AvailabilityId kNoEntry{999};

class AvailabilityRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedPlayers();
    }

    [[nodiscard]] AvailabilityRepository repository() { return AvailabilityRepository(*m_db); }

    static AvailabilityDraft draft(AvailabilityType type = AvailabilityType::Injury) {
        AvailabilityDraft result;
        result.type = type;
        result.fromDate = kFrom;
        return result;
    }

    AvailabilityId create(const AvailabilityDraft& entry, PlayerId player = kPlayer) {
        const auto id = repository().create(player, entry);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(AvailabilityId{});
    }

    // The only entry of the player.
    [[nodiscard]] Availability stored(PlayerId player = kPlayer) {
        const auto entries = repository().listByPlayer(player);
        EXPECT_TRUE(entries.has_value());
        EXPECT_EQ(entries.value_or(std::vector<Availability>{}).size(), 1U);
        return entries && !entries->empty() ? entries->front() : Availability{};
    }

    [[nodiscard]] std::size_t activeCount(Date date) {
        const auto entries = repository().listActiveOn(kTeam, date);
        EXPECT_TRUE(entries.has_value());
        return entries ? entries->size() : 0U;
    }
};

} // namespace

TEST_F(AvailabilityRepositoryTest, CreatedEntryKeepsEveryField) {
    AvailabilityDraft full = draft(AvailabilityType::Illness);
    full.expectedTo = kExpected;
    full.actualTo = kDayAfterExpected;
    full.description = "  grypa ";
    const AvailabilityId id = create(full);

    const Availability entry = stored();
    EXPECT_EQ(entry.id, id);
    EXPECT_EQ(entry.playerId, kPlayer);
    EXPECT_EQ(entry.type, AvailabilityType::Illness);
    EXPECT_EQ(entry.fromDate, kFrom);
    EXPECT_EQ(entry.expectedTo, kExpected);
    EXPECT_EQ(entry.actualTo, kDayAfterExpected);
    EXPECT_EQ(entry.description, "grypa");
}

TEST_F(AvailabilityRepositoryTest, GetReadsOneEntryAndAMissingOneIsNotFound) {
    AvailabilityDraft full = draft(AvailabilityType::Illness);
    full.description = "grypa";
    const AvailabilityId id = create(full);

    const auto entry = repository().get(id);
    ASSERT_TRUE(entry.has_value()) << entry.error().detail;
    EXPECT_EQ(*entry, stored());
    const auto missing = repository().get(AvailabilityId{id.value + 1});
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST_F(AvailabilityRepositoryTest, OptionalFieldsStayEmpty) {
    create(draft());
    const Availability entry = stored();
    EXPECT_EQ(entry.expectedTo, std::nullopt);
    EXPECT_EQ(entry.actualTo, std::nullopt);
    EXPECT_EQ(entry.description, std::nullopt);
}

TEST_F(AvailabilityRepositoryTest, EntryOfAMissingPlayerIsInvalidInput) {
    const auto id = repository().create(kNoPlayer, draft());
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(AvailabilityRepositoryTest, EndBeforeTheStartIsInvalidInput) {
    AvailabilityDraft bad = draft();
    bad.expectedTo = kDayBefore;
    const auto id = repository().create(kPlayer, bad);
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

// A broken optional date must not turn into NULL, which the column would accept.
TEST_F(AvailabilityRepositoryTest, ImpossibleDateIsInvalidInput) {
    AvailabilityDraft bad = draft();
    bad.expectedTo = 2026y / February / 30d;
    const auto id = repository().create(kPlayer, bad);
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(AvailabilityRepositoryTest, ListByPlayerIsNewestFirstAndOnlyThatPlayer) {
    AvailabilityDraft older = draft();
    older.fromDate = kDayBefore;
    const AvailabilityId first = create(older);
    const AvailabilityId second = create(draft());
    create(draft(), kOtherPlayer);

    const auto entries = repository().listByPlayer(kPlayer);
    ASSERT_TRUE(entries.has_value()) << entries.error().detail;
    ASSERT_EQ(entries->size(), 2U);
    EXPECT_EQ(entries->at(0).id, second);
    EXPECT_EQ(entries->at(1).id, first);
}

// --- Active on a date ---

TEST_F(AvailabilityRepositoryTest, ActiveListStartsOnTheFirstDay) {
    create(draft());
    EXPECT_EQ(activeCount(kDayBefore), 0U);
    EXPECT_EQ(activeCount(kFrom), 1U);
}

TEST_F(AvailabilityRepositoryTest, ActiveListEndsOnTheReturnDay) {
    const AvailabilityId id = create(draft());
    ASSERT_TRUE(repository().close(id, kExpected).has_value());
    EXPECT_EQ(activeCount(kDayBefore), 0U);
    EXPECT_EQ(activeCount(2026y / November / 19d), 1U);
    EXPECT_EQ(activeCount(kExpected), 0U);
}

TEST_F(AvailabilityRepositoryTest, InjuryStaysActivePastTheExpectedEnd) {
    AvailabilityDraft injury = draft(AvailabilityType::Injury);
    injury.expectedTo = kExpected;
    create(injury);
    EXPECT_EQ(activeCount(kExpected), 1U);
    EXPECT_EQ(activeCount(kDayAfterExpected), 1U);
}

TEST_F(AvailabilityRepositoryTest, SuspensionEndsAfterTheExpectedEnd) {
    AvailabilityDraft suspension = draft(AvailabilityType::Suspension);
    suspension.expectedTo = kExpected;
    create(suspension);
    EXPECT_EQ(activeCount(kExpected), 1U);
    EXPECT_EQ(activeCount(kDayAfterExpected), 0U);
}

TEST_F(AvailabilityRepositoryTest, ActiveListCoversOnlyTheTeam) {
    ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, primary_position) "
                    "VALUES (3, 2, 'Piotr', 'Lis', 'gk')"));
    create(draft());
    create(draft(), PlayerId{3});

    const auto entries = repository().listActiveOn(kTeam, kFrom);
    ASSERT_TRUE(entries.has_value()) << entries.error().detail;
    ASSERT_EQ(entries->size(), 1U);
    EXPECT_EQ(entries->at(0).playerId, kPlayer);
}

TEST_F(AvailabilityRepositoryTest, ActiveListSkipsPlayersArchivedByThatDate) {
    create(draft());
    ASSERT_TRUE(sql("UPDATE player SET archived_at = '2026-11-15' WHERE id = 1"));
    EXPECT_EQ(activeCount(2026y / November / 14d), 1U);
    EXPECT_EQ(activeCount(2026y / November / 15d), 0U);
    EXPECT_EQ(activeCount(2026y / November / 16d), 0U);
}

TEST_F(AvailabilityRepositoryTest, ActiveListReturnsOverlappingEntries) {
    create(draft(AvailabilityType::Injury));
    create(draft(AvailabilityType::Suspension));
    const auto entries = repository().listActiveOn(kTeam, kFrom);
    ASSERT_TRUE(entries.has_value());
    ASSERT_EQ(entries->size(), 2U);
    EXPECT_EQ(playerStatusOn(*entries, kFrom), PlayerStatus::Injured);
}

// --- Update ---

TEST_F(AvailabilityRepositoryTest, UpdateChangesTheFields) {
    const AvailabilityId id = create(draft());
    AvailabilityDraft changed = draft(AvailabilityType::Absence);
    changed.fromDate = kDayBefore;
    changed.expectedTo = kExpected;
    changed.actualTo = kDayAfterExpected;
    changed.description = "urlop";
    ASSERT_TRUE(repository().update(id, changed).has_value());

    const Availability entry = stored();
    EXPECT_EQ(entry.type, AvailabilityType::Absence);
    EXPECT_EQ(entry.fromDate, kDayBefore);
    EXPECT_EQ(entry.expectedTo, kExpected);
    EXPECT_EQ(entry.actualTo, kDayAfterExpected);
    EXPECT_EQ(entry.description, "urlop");
}

TEST_F(AvailabilityRepositoryTest, UpdateWithoutADescriptionKeepsTheStoredOne) {
    AvailabilityDraft original = draft();
    original.description = "zerwane więzadło";
    const AvailabilityId id = create(original);

    AvailabilityDraft fromAssistant = draft(AvailabilityType::Illness);
    fromAssistant.description = std::nullopt;
    ASSERT_TRUE(repository().update(id, fromAssistant).has_value());

    const Availability entry = stored();
    EXPECT_EQ(entry.type, AvailabilityType::Illness);
    EXPECT_EQ(entry.description, "zerwane więzadło");
}

TEST_F(AvailabilityRepositoryTest, UpdateWithAnEmptyDescriptionClearsIt) {
    AvailabilityDraft original = draft();
    original.description = "uraz";
    const AvailabilityId id = create(original);

    AvailabilityDraft cleared = draft();
    cleared.description = "  ";
    ASSERT_TRUE(repository().update(id, cleared).has_value());
    EXPECT_EQ(stored().description, std::nullopt);
}

TEST_F(AvailabilityRepositoryTest, UpdateCanReopenAClosedEntry) {
    const AvailabilityId id = create(draft());
    ASSERT_TRUE(repository().close(id, kExpected).has_value());
    ASSERT_TRUE(repository().update(id, draft()).has_value());
    EXPECT_EQ(stored().actualTo, std::nullopt);
}

TEST_F(AvailabilityRepositoryTest, UpdateOfAMissingEntryIsNotFound) {
    for (const std::optional<std::string>& description :
         {std::optional<std::string>{}, std::optional<std::string>{"opis"}}) {
        AvailabilityDraft changed = draft();
        changed.description = description;
        const auto status = repository().update(kNoEntry, changed);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, ErrorCode::NotFound);
    }
}

// --- Close and delete ---

TEST_F(AvailabilityRepositoryTest, CloseSetsOnlyTheActualEnd) {
    AvailabilityDraft original = draft();
    original.expectedTo = kExpected;
    original.description = "uraz";
    const AvailabilityId id = create(original);
    ASSERT_TRUE(repository().close(id, kDayAfterExpected).has_value());

    const Availability entry = stored();
    EXPECT_EQ(entry.actualTo, kDayAfterExpected);
    EXPECT_EQ(entry.expectedTo, kExpected);
    EXPECT_EQ(entry.description, "uraz");
}

TEST_F(AvailabilityRepositoryTest, CloseBeforeTheStartIsInvalidInputAndChangesNothing) {
    const AvailabilityId id = create(draft());
    const auto status = repository().close(id, kDayBefore);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(stored().actualTo, std::nullopt);
}

TEST_F(AvailabilityRepositoryTest, CloseOfAMissingEntryIsNotFound) {
    const auto status = repository().close(kNoEntry, kFrom);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
}

TEST_F(AvailabilityRepositoryTest, RemoveDeletesTheEntry) {
    const AvailabilityId id = create(draft());
    ASSERT_TRUE(repository().remove(id).has_value());
    EXPECT_EQ(count("SELECT count(*) FROM availability"), 0);
}

TEST_F(AvailabilityRepositoryTest, RemoveOfAMissingEntryIsNotFound) {
    const auto status = repository().remove(kNoEntry);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
}
