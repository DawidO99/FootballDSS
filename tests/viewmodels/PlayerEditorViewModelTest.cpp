#include "viewmodels/PlayerEditorViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Player.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/ErrorPresenter.hpp"

#include <QVariantMap>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::ErrorPresenter;
using fdss::viewmodels::PlayerEditorViewModel;

namespace {

class PlayerEditorViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
    }

    PlayerId addPlayer(int number) {
        PlayerDraft draft;
        draft.firstName = "Jan";
        draft.lastName = "Kowalski";
        draft.shirtNumber = number;
        draft.primaryPosition = Position::Cb;
        draft.secondaryPositions = {Position::Rb};
        draft.birthYear = 2000;
        draft.foot = PreferredFoot::Left;
        draft.notes = "Lider";
        const auto id = registry().squad().addPlayer(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(PlayerId{});
    }

    void fillValid(PlayerEditorViewModel& editor) const {
        editor.setTeamId(m_team.value);
        editor.startAdding();
        editor.setFirstName(QStringLiteral("Piotr"));
        editor.setLastName(QStringLiteral("Wiśniewski"));
    }

    TeamId m_team;
};

TEST_F(PlayerEditorViewModelTest, AddsAPlayerWithEveryField) {
    PlayerEditorViewModel editor;
    fillValid(editor);
    editor.setShirtNumber(10);
    editor.setBirthYear(1999);
    editor.setPrimaryPosition(QStringLiteral("cm"));
    editor.setSecondaryPositions({QStringLiteral("am"), QStringLiteral("dm")});
    editor.setFoot(QStringLiteral("right"));
    editor.setNotes(QStringLiteral("Kapitan"));
    SignalCount saved(&editor, &PlayerEditorViewModel::saved);

    ASSERT_TRUE(editor.save());
    EXPECT_EQ(saved.count(), 1);

    const auto players = registry().squad().listPlayers(m_team);
    ASSERT_TRUE(players.has_value());
    ASSERT_EQ(players->size(), 1U);
    const Player& player = players->front().player;
    EXPECT_EQ(player.firstName, "Piotr");
    EXPECT_EQ(player.lastName, "Wiśniewski");
    EXPECT_EQ(player.shirtNumber, 10);
    EXPECT_EQ(player.birthYear, 1999);
    EXPECT_EQ(player.primaryPosition, Position::Cm);
    EXPECT_EQ(player.secondaryPositions, (std::vector{Position::Am, Position::Dm}));
    EXPECT_EQ(player.foot, PreferredFoot::Right);
    EXPECT_EQ(player.notes, "Kapitan");
}

TEST_F(PlayerEditorViewModelTest, OptionalNumbersMayStayEmpty) {
    PlayerEditorViewModel editor;
    fillValid(editor);
    editor.setShirtNumber(5);
    editor.setShirtNumber(QVariant{});
    ASSERT_TRUE(editor.save());
    const auto players = registry().squad().listPlayers(m_team);
    EXPECT_FALSE(players->front().player.shirtNumber.has_value());
    EXPECT_FALSE(players->front().player.birthYear.has_value());
    EXPECT_FALSE(players->front().player.notes.has_value());
}

TEST_F(PlayerEditorViewModelTest, EditingShowsThePlayerAndSavesOverIt) {
    const PlayerId id = addPlayer(7);
    PlayerEditorViewModel editor;
    ASSERT_TRUE(editor.startEditing(id.value));
    EXPECT_FALSE(editor.adding());
    EXPECT_EQ(editor.firstName(), QStringLiteral("Jan"));
    EXPECT_EQ(editor.shirtNumber().toInt(), 7);
    EXPECT_EQ(editor.birthYear().toInt(), 2000);
    EXPECT_EQ(editor.primaryPosition(), QStringLiteral("cb"));
    EXPECT_EQ(editor.secondaryPositions(), QStringList{QStringLiteral("rb")});
    EXPECT_EQ(editor.foot(), QStringLiteral("left"));
    EXPECT_EQ(editor.notes(), QStringLiteral("Lider"));

    editor.setShirtNumber(8);
    ASSERT_TRUE(editor.save());
    EXPECT_EQ(registry().squad().player(id)->player.shirtNumber, 8);
    EXPECT_EQ(registry().squad().listPlayers(m_team)->size(), 1U);
}

TEST_F(PlayerEditorViewModelTest, StartAddingForgetsTheEditedPlayer) {
    const PlayerId id = addPlayer(7);
    PlayerEditorViewModel editor;
    ASSERT_TRUE(editor.startEditing(id.value));
    editor.startAdding();
    EXPECT_TRUE(editor.adding());
    EXPECT_TRUE(editor.firstName().isEmpty());
    EXPECT_FALSE(editor.shirtNumber().isValid());
    EXPECT_EQ(editor.foot(), QStringLiteral("unknown"));
}

TEST_F(PlayerEditorViewModelTest, EveryBrokenFieldShowsItsError) {
    PlayerEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding();
    editor.setShirtNumber(100);
    editor.setBirthYear(1900);

    EXPECT_FALSE(editor.save());
    EXPECT_EQ(editor.firstNameError(), ErrorPresenter::invalidInputMessage("first_name"));
    EXPECT_EQ(editor.lastNameError(), ErrorPresenter::invalidInputMessage("last_name"));
    EXPECT_EQ(editor.shirtNumberError(), ErrorPresenter::invalidInputMessage("shirt_number"));
    EXPECT_EQ(editor.birthYearError(), ErrorPresenter::invalidInputMessage("birth_year"));
    EXPECT_TRUE(registry().squad().listPlayers(m_team)->empty());

    editor.setFirstName(QStringLiteral("Jan"));
    EXPECT_TRUE(editor.firstNameError().isEmpty());
    EXPECT_FALSE(editor.lastNameError().isEmpty());
}

TEST_F(PlayerEditorViewModelTest, DuplicateNumberShowsTheErrorOnTheField) {
    addPlayer(7);
    PlayerEditorViewModel editor;
    fillValid(editor);
    editor.setShirtNumber(7);
    SignalCount toasts(&editor, &PlayerEditorViewModel::errorOccurred);

    EXPECT_FALSE(editor.save());
    EXPECT_EQ(editor.shirtNumberError(),
              ErrorPresenter::message(Error{ErrorCode::Conflict, "", "shirt_number"}));
    EXPECT_FALSE(editor.shirtNumberError().isEmpty());
    EXPECT_EQ(toasts.count(), 0);

    editor.setShirtNumber(8);
    EXPECT_TRUE(editor.shirtNumberError().isEmpty());
    EXPECT_TRUE(editor.save());
}

TEST_F(PlayerEditorViewModelTest, ThePrimaryPositionDropsOutOfTheSecondaryOnes) {
    PlayerEditorViewModel editor;
    fillValid(editor);
    editor.setPrimaryPosition(QStringLiteral("cm"));
    editor.setSecondaryPositions({QStringLiteral("am"), QStringLiteral("cb")});
    SignalCount options(&editor, &PlayerEditorViewModel::secondaryOptionsChanged);

    editor.setPrimaryPosition(QStringLiteral("cb"));

    EXPECT_EQ(editor.secondaryPositions(), QStringList{QStringLiteral("am")});
    EXPECT_EQ(options.count(), 1);
    // The 14 positions after "—", not given (P19).
    EXPECT_EQ(editor.positionOptions().size(), 15);
    const QVariantList secondary = editor.secondaryPositionOptions();
    EXPECT_EQ(secondary.size(), 13);
    for (const QVariant& option : secondary) {
        EXPECT_NE(option.toMap().value(QStringLiteral("value")).toString(), QStringLiteral("cb"));
    }
    EXPECT_TRUE(editor.save());
}

// P19 decision 8: a new player starts without a primary position, and "—" can be chosen again.
TEST_F(PlayerEditorViewModelTest, ThePrimaryPositionMayBeNotGiven) {
    PlayerEditorViewModel editor;
    editor.startAdding();
    EXPECT_TRUE(editor.primaryPosition().isEmpty());
    EXPECT_TRUE(editor.positionOptions()
                    .constFirst()
                    .toMap()
                    .value(QStringLiteral("value"))
                    .toString()
                    .isEmpty());
    fillValid(editor);
    editor.setPrimaryPosition(QStringLiteral("st"));
    editor.setSecondaryPositions({QStringLiteral("rw")});

    editor.setPrimaryPosition(QString{});

    EXPECT_TRUE(editor.primaryPosition().isEmpty());
    EXPECT_EQ(editor.secondaryPositions(), QStringList{QStringLiteral("rw")});
    EXPECT_EQ(editor.secondaryPositionOptions().size(), 14);
    ASSERT_TRUE(editor.save());
    const auto players = registry().squad().listPlayers(m_team);
    ASSERT_TRUE(players.has_value());
    ASSERT_EQ(players->size(), 1U);
    EXPECT_FALSE(players->front().player.primaryPosition.has_value());
    EXPECT_EQ(players->front().player.secondaryPositions, std::vector{Position::Rw});
}

TEST_F(PlayerEditorViewModelTest, EachNumberHasASignalOfItsOwn) {
    PlayerEditorViewModel editor;
    fillValid(editor);
    SignalCount numbers(&editor, &PlayerEditorViewModel::shirtNumberChanged);
    SignalCount years(&editor, &PlayerEditorViewModel::birthYearChanged);

    editor.setFirstName(QStringLiteral("Inny"));
    editor.setFoot(QStringLiteral("left"));
    EXPECT_EQ(numbers.count(), 0);
    EXPECT_EQ(years.count(), 0);

    editor.setShirtNumber(5);
    EXPECT_EQ(numbers.count(), 1);
    EXPECT_EQ(years.count(), 0);
}

TEST_F(PlayerEditorViewModelTest, LimitsComeFromTheCore) {
    PlayerEditorViewModel editor;
    EXPECT_EQ(editor.minShirtNumber(), 1);
    EXPECT_EQ(editor.maxShirtNumber(), 99);
    EXPECT_EQ(editor.minBirthYear(), 1950);
    EXPECT_EQ(editor.maxBirthYear(), 2026 - 4);
}

TEST_F(PlayerEditorViewModelTest, TheBirthYearLimitFollowsNewYearWhenTheFormStarts) {
    const PlayerId id = addPlayer(7);
    PlayerEditorViewModel editor;
    fillValid(editor);
    ASSERT_EQ(editor.maxBirthYear(), 2022);
    SignalCount changed(&editor, &PlayerEditorViewModel::maxBirthYearChanged);

    // A form that started last year keeps its limit, and the next start in that year changes
    // nothing.
    editor.startAdding();
    EXPECT_EQ(changed.count(), 0);

    // From 2026-11-01 12:00 to 2027-01-01 12:00.
    m_clock->advance(std::chrono::days{61});
    EXPECT_EQ(editor.maxBirthYear(), 2022);

    editor.startAdding();
    EXPECT_EQ(editor.maxBirthYear(), 2023);
    EXPECT_EQ(changed.count(), 1);

    // The new limit is the one the core checks, in a form opened for editing as well.
    ASSERT_TRUE(editor.startEditing(id.value));
    EXPECT_EQ(changed.count(), 1);
    editor.setBirthYear(2023);
    EXPECT_TRUE(editor.save());
    EXPECT_EQ(registry().squad().player(id)->player.birthYear, 2023);
}

TEST_F(PlayerEditorViewModelTest, EditingAfterNewYearReadsTheLimitToo) {
    const PlayerId id = addPlayer(7);
    PlayerEditorViewModel editor;
    m_clock->advance(std::chrono::days{61});
    ASSERT_TRUE(editor.startEditing(id.value));
    EXPECT_EQ(editor.maxBirthYear(), 2023);
}

TEST_F(PlayerEditorViewModelTest, RoleWithoutEditSquadIsRefused) {
    const StaffUserId physio = addStaff("Fizjo", Role::Physio);
    registry().auth().logout();
    ASSERT_TRUE(registry().auth().login(physio, kPin).has_value());

    PlayerEditorViewModel editor;
    fillValid(editor);
    SignalCount errors(&editor, &PlayerEditorViewModel::errorOccurred);
    EXPECT_FALSE(editor.save());
    EXPECT_EQ(errors.count(), 1);
    EXPECT_TRUE(registry().squad().listPlayers(m_team)->empty());
}

TEST_F(PlayerEditorViewModelTest, EditingAMissingPlayerSaysWhy) {
    PlayerEditorViewModel editor;
    SignalCount errors(&editor, &PlayerEditorViewModel::errorOccurred);
    EXPECT_FALSE(editor.startEditing(999));
    EXPECT_EQ(errors.count(), 1);
}

} // namespace
