#include "services/SquadService.hpp"

#include "ServiceFixture.hpp"
#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::services;
using namespace fdss::tests;

namespace {

constexpr Date kToday{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}};
constexpr Date kLastMonth{std::chrono::year{2026}, std::chrono::October, std::chrono::day{1}};
constexpr Date kNextMonth{std::chrono::year{2026}, std::chrono::December, std::chrono::day{1}};
constexpr auto kSecret = "Naderwany mięsień dwugłowy";

class SquadServiceTest : public ServiceTest {
protected:
    void SetUp() override {
        ServiceTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        seedPlayers();
        if (HasFatalFailure()) {
            return;
        }
        m_players = std::make_unique<PlayerRepository>(*m_db);
        m_availability = std::make_unique<AvailabilityRepository>(*m_db);
        m_squad = std::make_unique<SquadService>(*m_players, *m_availability, m_session, m_clock);
    }

    void logInAs(Role role) {
        const StaffUserId id = addUser(std::string(toDbText(role)).c_str(), role);
        logIn(id);
    }

    static PlayerDraft playerDraft(std::optional<int> number = 11) {
        PlayerDraft draft;
        draft.firstName = "Piotr";
        draft.lastName = "Wiśniewski";
        draft.shirtNumber = number;
        draft.primaryPosition = Position::Cb;
        return draft;
    }

    static AvailabilityDraft injury(std::optional<std::string> description = std::nullopt) {
        AvailabilityDraft draft;
        draft.type = AvailabilityType::Injury;
        draft.fromDate = kLastMonth;
        draft.description = std::move(description);
        return draft;
    }

    // Straight through the repository: no session needed.
    AvailabilityId addEntry(const AvailabilityDraft& draft, PlayerId player = kPlayer) {
        const auto id = m_availability->create(player, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(AvailabilityId{});
    }

    [[nodiscard]] Availability storedEntry(AvailabilityId id) const {
        const auto entries = m_availability->listByPlayer(kPlayer);
        EXPECT_TRUE(entries.has_value());
        const auto entry = std::ranges::find(*entries, id, &Availability::id);
        return entry != entries->end() ? *entry : Availability{};
    }

    std::unique_ptr<PlayerRepository> m_players;
    std::unique_ptr<AvailabilityRepository> m_availability;
    std::unique_ptr<SquadService> m_squad;
};

struct GuardedCall {
    const char* name;
    Permission permission;
    std::function<Status(SquadService&)> run;
};

template <class T> Status statusOf(const Result<T>& result) {
    return result ? Status{} : Status{std::unexpected(result.error())};
}

std::vector<GuardedCall> everyGuardedCall() {
    constexpr TeamId team{1};
    constexpr PlayerId player{1};
    constexpr AvailabilityId entry{1};
    PlayerDraft validPlayer;
    validPlayer.firstName = "Piotr";
    validPlayer.lastName = "Wiśniewski";
    AvailabilityDraft validEntry;
    validEntry.fromDate = kLastMonth;
    return {
        {"addPlayer", Permission::EditSquad,
         [=](SquadService& squad) { return statusOf(squad.addPlayer(team, validPlayer)); }},
        {"updatePlayer", Permission::EditSquad,
         [=](SquadService& squad) { return squad.updatePlayer(player, validPlayer); }},
        {"archivePlayer", Permission::EditSquad,
         [=](SquadService& squad) { return squad.archivePlayer(player); }},
        {"unarchivePlayer", Permission::EditSquad,
         [=](SquadService& squad) { return squad.unarchivePlayer(player); }},
        {"addAvailability", Permission::EditAvailability,
         [=](SquadService& squad) { return statusOf(squad.addAvailability(player, validEntry)); }},
        {"updateAvailability", Permission::EditAvailability,
         [=](SquadService& squad) { return squad.updateAvailability(entry, validEntry); }},
        {"markReturned", Permission::EditAvailability,
         [=](SquadService& squad) { return squad.markReturned(player, entry, kToday); }},
    };
}

TEST_F(SquadServiceTest, EveryRoleIsRefusedWhatItsPermissionsDoNotAllow) {
    addEntry(injury());
    for (const Role role : allValues<Role>()) {
        logInAs(role);
        for (const GuardedCall& call : everyGuardedCall()) {
            SCOPED_TRACE(std::string(call.name) + " as " + std::string(toDbText(role)));
            const Status status = call.run(*m_squad);
            if (isAllowed(role, call.permission)) {
                EXPECT_TRUE(status.has_value()) << status.error().detail;
            } else {
                expectError(status, ErrorCode::PermissionDenied);
            }
            // An allowed archive must not change what the next call meets.
            ASSERT_TRUE(m_players->unarchive(kPlayer).has_value());
        }
        m_session.clear();
    }
}

TEST_F(SquadServiceTest, NothingWorksWithoutALogin) {
    for (const GuardedCall& call : everyGuardedCall()) {
        SCOPED_TRACE(call.name);
        expectError(call.run(*m_squad), ErrorCode::PermissionDenied);
    }
    expectError(m_squad->listPlayers(kTeam), ErrorCode::PermissionDenied);
    expectError(m_squad->player(kPlayer), ErrorCode::PermissionDenied);
    expectError(m_squad->listAvailability(kPlayer), ErrorCode::PermissionDenied);
}

TEST_F(SquadServiceTest, EveryLoggedInRoleReadsTheSquad) {
    for (const Role role : allValues<Role>()) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        const auto players = m_squad->listPlayers(kTeam);
        ASSERT_TRUE(players.has_value()) << players.error().detail;
        EXPECT_EQ(players->size(), 2U);
        EXPECT_TRUE(m_squad->player(kPlayer).has_value());
        EXPECT_TRUE(m_squad->listAvailability(kPlayer).has_value());
        m_session.clear();
    }
}

TEST_F(SquadServiceTest, ListShowsTheStatusOfToday) {
    addEntry(injury());
    AvailabilityDraft future = injury();
    future.type = AvailabilityType::Absence;
    future.fromDate = kNextMonth;
    addEntry(future, kOtherPlayer);
    logInAs(Role::Analyst);

    const auto players = m_squad->listPlayers(kTeam);
    ASSERT_TRUE(players.has_value());
    ASSERT_EQ(players->size(), 2U);
    EXPECT_EQ(players->at(0).status, PlayerStatus::Injured);
    EXPECT_EQ(players->at(1).status, PlayerStatus::Available);
    EXPECT_EQ(m_squad->player(kPlayer)->status, PlayerStatus::Injured);
}

TEST_F(SquadServiceTest, ArchiveUsesTodayAndUnarchiveBringsThePlayerBack) {
    logInAs(Role::Assistant);
    ASSERT_TRUE(m_squad->archivePlayer(kPlayer).has_value());
    EXPECT_EQ(m_squad->player(kPlayer)->player.archivedAt, kToday);
    // The list holds the archived player too: the viewmodel filters.
    EXPECT_EQ(m_squad->listPlayers(kTeam)->size(), 2U);

    ASSERT_TRUE(m_squad->unarchivePlayer(kPlayer).has_value());
    EXPECT_FALSE(m_squad->player(kPlayer)->player.archivedAt.has_value());
}

TEST_F(SquadServiceTest, DuplicateShirtNumberIsAConflictOnTheField) {
    logInAs(Role::Coach);
    expectError(m_squad->addPlayer(kTeam, playerDraft(7)), ErrorCode::Conflict, "shirt_number");
    const auto id = m_squad->addPlayer(kTeam, playerDraft(11));
    ASSERT_TRUE(id.has_value());
    expectError(m_squad->updatePlayer(*id, playerDraft(9)), ErrorCode::Conflict, "shirt_number");
}

TEST_F(SquadServiceTest, UnarchiveOntoATakenShirtNumberIsAConflictOnTheField) {
    logInAs(Role::Coach);
    ASSERT_TRUE(m_squad->archivePlayer(kPlayer).has_value());
    ASSERT_TRUE(m_squad->addPlayer(kTeam, playerDraft(7)).has_value());

    expectError(m_squad->unarchivePlayer(kPlayer), ErrorCode::Conflict, "shirt_number");
    EXPECT_TRUE(m_squad->player(kPlayer)->player.archivedAt.has_value());
}

TEST_F(SquadServiceTest, PlayerDraftIsValidatedAgainstTheCurrentYear) {
    logInAs(Role::Coach);
    PlayerDraft draft = playerDraft();
    draft.birthYear = 2026 - kYoungestPlayerAge;
    EXPECT_TRUE(m_squad->addPlayer(kTeam, draft).has_value());

    draft.shirtNumber = 12;
    draft.birthYear = 2026 - kYoungestPlayerAge + 1;
    expectError(m_squad->addPlayer(kTeam, draft), ErrorCode::InvalidInput, "birth_year");
    draft.birthYear.reset();
    draft.firstName = " ";
    expectError(m_squad->updatePlayer(kPlayer, draft), ErrorCode::InvalidInput, "first_name");
}

TEST_F(SquadServiceTest, DescriptionIsHiddenFromRolesWithoutDetailsAtTheBoundary) {
    addEntry(injury(kSecret));
    for (const Role role : allValues<Role>()) {
        SCOPED_TRACE(toDbText(role));
        logInAs(role);
        const auto entries = m_squad->listAvailability(kPlayer);
        ASSERT_TRUE(entries.has_value());
        ASSERT_EQ(entries->size(), 1U);
        EXPECT_EQ(entries->front().type, AvailabilityType::Injury);
        EXPECT_EQ(entries->front().fromDate, kLastMonth);
        if (isAllowed(role, Permission::ViewAvailabilityDetails)) {
            EXPECT_EQ(entries->front().description, kSecret);
        } else {
            EXPECT_FALSE(entries->front().description.has_value());
        }
        m_session.clear();
    }
}

TEST_F(SquadServiceTest, AssistantSavingAnEntryKeepsTheDescription) {
    const AvailabilityId id = addEntry(injury(kSecret));
    logInAs(Role::Assistant);

    const auto entries = m_squad->listAvailability(kPlayer);
    ASSERT_TRUE(entries.has_value());
    AvailabilityDraft edited = entries->front();
    edited.expectedTo = kNextMonth;
    ASSERT_TRUE(m_squad->updateAvailability(id, edited).has_value());

    const Availability stored = storedEntry(id);
    EXPECT_EQ(stored.expectedTo, kNextMonth);
    EXPECT_EQ(stored.description, kSecret);
}

TEST_F(SquadServiceTest, RoleWithoutDetailsCannotSetADescription) {
    const AvailabilityId id = addEntry(injury(kSecret));
    logInAs(Role::Assistant);

    expectError(m_squad->addAvailability(kPlayer, injury("Coś")), ErrorCode::PermissionDenied,
                "description");
    expectError(m_squad->updateAvailability(id, injury("")), ErrorCode::PermissionDenied,
                "description");
    EXPECT_EQ(storedEntry(id).description, kSecret);
    EXPECT_TRUE(m_squad->addAvailability(kPlayer, injury()).has_value());
}

TEST_F(SquadServiceTest, PhysioSetsAndClearsADescription) {
    logInAs(Role::Physio);
    const auto id = m_squad->addAvailability(kPlayer, injury(kSecret));
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(storedEntry(*id).description, kSecret);

    ASSERT_TRUE(m_squad->updateAvailability(*id, injury("")).has_value());
    EXPECT_FALSE(storedEntry(*id).description.has_value());
}

TEST_F(SquadServiceTest, AvailabilityDraftIsValidated) {
    logInAs(Role::Coach);
    AvailabilityDraft draft = injury();
    draft.expectedTo = Date{std::chrono::year{2026}, std::chrono::September, std::chrono::day{1}};
    expectError(m_squad->addAvailability(kPlayer, draft), ErrorCode::InvalidInput, "expected_to");
}

TEST_F(SquadServiceTest, MarkReturnedSetsTheFirstAvailableDay) {
    const AvailabilityId id = addEntry(injury());
    logInAs(Role::Physio);
    ASSERT_EQ(m_squad->player(kPlayer)->status, PlayerStatus::Injured);

    ASSERT_TRUE(m_squad->markReturned(kPlayer, id, kToday).has_value());

    EXPECT_EQ(storedEntry(id).actualTo, kToday);
    EXPECT_EQ(m_squad->player(kPlayer)->status, PlayerStatus::Available);
}

TEST_F(SquadServiceTest, MarkReturnedBeforeTheStartNamesTheField) {
    const AvailabilityId id = addEntry(injury());
    logInAs(Role::Coach);
    const Date before{std::chrono::year{2026}, std::chrono::September, std::chrono::day{1}};

    expectError(m_squad->markReturned(kPlayer, id, before), ErrorCode::InvalidInput, "actual_to");
    EXPECT_FALSE(storedEntry(id).actualTo.has_value());
}

TEST_F(SquadServiceTest, MarkReturnedOfAnEntryOfAnotherPlayerIsNotFound) {
    const AvailabilityId id = addEntry(injury());
    logInAs(Role::Coach);

    expectError(m_squad->markReturned(kOtherPlayer, id, kToday), ErrorCode::NotFound);
    EXPECT_FALSE(storedEntry(id).actualTo.has_value());
}

} // namespace
