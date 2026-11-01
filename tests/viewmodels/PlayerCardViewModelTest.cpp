#include "viewmodels/PlayerCardViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/Player.hpp"
#include "services/SquadService.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::PlayerCardViewModel;

namespace {

class PlayerCardViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        PlayerDraft draft;
        draft.firstName = "Jan";
        draft.lastName = "Kowalski";
        draft.shirtNumber = 7;
        draft.birthYear = 2000;
        draft.primaryPosition = Position::Cb;
        draft.secondaryPositions = {Position::Rb, Position::Lb};
        draft.foot = PreferredFoot::Left;
        draft.notes = "Lider";
        const auto id =
            registry().squad().addPlayer(registry().setup().clubContext()->team.id, draft);
        ASSERT_TRUE(id.has_value());
        m_player = *id;
    }

    PlayerId m_player;
};

TEST_F(PlayerCardViewModelTest, ShowsThePlayer) {
    PlayerCardViewModel card;
    SignalCount changed(&card, &PlayerCardViewModel::playerChanged);
    card.setPlayerId(m_player.value);
    EXPECT_EQ(changed.count(), 1);

    EXPECT_EQ(card.number().toInt(), 7);
    EXPECT_EQ(card.name(), QStringLiteral("Jan Kowalski"));
    EXPECT_EQ(card.positionCode(), QStringLiteral("CB"));
    EXPECT_FALSE(card.positionName().isEmpty());
    EXPECT_EQ(card.secondaryPositions(), QStringLiteral("RB, LB"));
    EXPECT_EQ(card.age().toInt(), 26);
    EXPECT_EQ(card.birthYear().toInt(), 2000);
    EXPECT_FALSE(card.footName().isEmpty());
    EXPECT_EQ(card.notes(), QStringLiteral("Lider"));
    EXPECT_EQ(card.statusKey(), QStringLiteral("available"));
    EXPECT_FALSE(card.archived());
    EXPECT_TRUE(card.loadError().isEmpty());
}

TEST_F(PlayerCardViewModelTest, WithoutAPlayerEverythingIsEmpty) {
    PlayerCardViewModel card;
    EXPECT_TRUE(card.name().isEmpty());
    EXPECT_FALSE(card.number().isValid());
    EXPECT_TRUE(card.statusKey().isEmpty());
    EXPECT_FALSE(card.archived());
    EXPECT_TRUE(card.loadError().isEmpty());
}

TEST_F(PlayerCardViewModelTest, ReloadShowsANewStatus) {
    PlayerCardViewModel card;
    card.setPlayerId(m_player.value);
    AvailabilityDraft draft;
    draft.type = AvailabilityType::Illness;
    draft.fromDate = Date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{1}};
    ASSERT_TRUE(registry().squad().addAvailability(m_player, draft).has_value());

    card.reload();
    EXPECT_EQ(card.statusKey(), QStringLiteral("ill"));
}

TEST_F(PlayerCardViewModelTest, AnArchivedPlayerShowsArchivedInsteadOfAStatus) {
    AvailabilityDraft draft;
    draft.fromDate = Date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{1}};
    ASSERT_TRUE(registry().squad().addAvailability(m_player, draft).has_value());
    ASSERT_TRUE(registry().squad().archivePlayer(m_player).has_value());

    PlayerCardViewModel card;
    card.setPlayerId(m_player.value);
    EXPECT_TRUE(card.archived());
    EXPECT_EQ(card.statusKey(), QStringLiteral("archived"));
    EXPECT_FALSE(card.statusText().isEmpty());
}

TEST_F(PlayerCardViewModelTest, AMissingPlayerGivesALoadError) {
    PlayerCardViewModel card;
    card.setPlayerId(999);
    EXPECT_FALSE(card.loadError().isEmpty());
    EXPECT_TRUE(card.name().isEmpty());
}

} // namespace
