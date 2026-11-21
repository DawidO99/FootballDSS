// Live tactics (packet P13b): the shape on the pitch at a minute from the saved lineup, the
// substitutions, the red cards and the tactic changes, and the edits of a live shape. Expected
// values are counted by hand from the catalogue (P12 decision 12).

#include "core/algorithms/Formations.hpp"
#include "core/algorithms/MatchMinutes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace fdss::core;

namespace {

PlayerId pid(std::int64_t id) { return PlayerId{id}; }

// 4-4-2 by slot: 0 GK, 1 RB, 2 CB, 3 CB, 4 LB, 5 RM, 6 CM, 7 CM, 8 LM, 9 ST, 10 ST, with the
// players 1–11 in slot order; bench 12–14.
Lineup lineup442() {
    Lineup lineup = emptyLineup(*findFormation("4-4-2"));
    for (std::size_t slot = 0; slot < lineup.pitchSlots.size(); ++slot) {
        lineup.pitchSlots[slot].player = pid(static_cast<std::int64_t>(slot) + 1);
    }
    lineup.bench = {pid(12), pid(13), pid(14)};
    return lineup;
}

MatchEventDraft sub(int minute, std::int64_t in, std::int64_t out) {
    return {.minute = minute,
            .type = MatchEventType::Sub,
            .playerId = pid(in),
            .relatedPlayerId = pid(out)};
}

MatchEventDraft red(int minute, std::int64_t player) {
    return {.minute = minute, .type = MatchEventType::Red, .playerId = pid(player)};
}

MatchTacticChangeDraft changeAt(int minute, const Lineup& shape) {
    return {.minute = minute, .addedTime = std::nullopt, .shape = shape};
}

const LineupSlot& slotOf(const Lineup& shape, std::int64_t player) {
    for (const LineupSlot& slot : shape.pitchSlots) {
        if (slot.player == pid(player)) {
            return slot;
        }
    }
    ADD_FAILURE() << "player " << player << " is not on the pitch";
    return shape.pitchSlots.front();
}

Lineup shapeOrFail(const Lineup& lineup, const std::vector<MatchEventDraft>& events,
                   const std::vector<MatchTacticChangeDraft>& changes,
                   std::optional<MatchTime> time) {
    auto shape = shapeAt(lineup, events, changes, time);
    EXPECT_TRUE(shape.has_value());
    return shape.value_or(Lineup{});
}

} // namespace

TEST(LiveShapeTest, ChangeShapeKeepsEveryPlayerAndMatchesPositions) {
    const Lineup start = lineup442();
    const Lineup shape = changeShape(start, *findFormation("4-2-3-1"));
    EXPECT_EQ(shape.formation, "4-2-3-1");
    // The players stay in their slots.
    for (std::size_t slot = 0; slot < shape.pitchSlots.size(); ++slot) {
        EXPECT_EQ(shape.pitchSlots[slot].player, start.pitchSlots[slot].player);
    }
    // GK, the back four, RM, LM and the left striker keep their positions.
    for (const std::int64_t player : {1, 2, 3, 4, 5, 6, 9, 10}) {
        EXPECT_EQ(slotOf(shape, player).position, slotOf(start, player).position) << player;
    }
    // The two central midfielders become the two DMs, the other striker the AM.
    EXPECT_EQ(slotOf(shape, 7).position, Position::Dm);
    EXPECT_EQ(slotOf(shape, 8).position, Position::Dm);
    EXPECT_LT(slotOf(shape, 7).x, slotOf(shape, 8).x);
    EXPECT_EQ(slotOf(shape, 11).position, Position::Am);
}

TEST(LiveShapeTest, ChangeShapeWithTenLeavesTheMostAttackingPositionEmpty) {
    Lineup ten = lineup442();
    ten.pitchSlots[2].player.reset(); // a centre back sent off
    const Lineup shape = changeShape(ten, *findFormation("4-4-2"));
    // The players move back to fill the four defenders; the empty slot takes the last striker.
    EXPECT_EQ(shape.formation, "4-4-1");
    EXPECT_EQ(shape.pitchSlots[2].position, Position::St);
    EXPECT_FALSE(shape.pitchSlots[2].player);
    EXPECT_TRUE(hasGoalkeeperOnPitch(shape));
}

TEST(LiveShapeTest, ChangeShapeNeverSendsAnOutfieldPlayerInGoalWhileAPositionIsFree) {
    Lineup ten = lineup442();
    ten.pitchSlots[0].player.reset(); // the goalkeeper sent off
    const Lineup shape = changeShape(ten, *findFormation("4-3-3"));
    // The empty slot keeps the goal; the ten take the outfield positions of 4-3-3.
    EXPECT_EQ(shape.pitchSlots[0].position, Position::Gk);
    EXPECT_FALSE(hasGoalkeeperOnPitch(shape));
    EXPECT_EQ(slotOf(shape, 9).position, Position::Dm); // LM
    EXPECT_EQ(slotOf(shape, 6).position, Position::Lw); // RM
    EXPECT_EQ(slotOf(shape, 11).position, Position::Rw);
    // Fewer than eleven: counted from the rows (catalogue names only with eleven).
    EXPECT_EQ(shape.formation, "4-1-2-2-1");
}

TEST(LiveShapeTest, SwapPlacesExchangesPositionsAndCoordinates) {
    Lineup shape = lineup442();
    const LineupSlot rightBack = shape.pitchSlots[1];
    const LineupSlot leftBack = shape.pitchSlots[4];
    EXPECT_EQ(swapPlaces(shape, 1, 4), LineupEdit::Done);
    EXPECT_EQ(shape.pitchSlots[1].position, leftBack.position);
    EXPECT_EQ(shape.pitchSlots[1].x, leftBack.x);
    EXPECT_EQ(shape.pitchSlots[4].position, rightBack.position);
    EXPECT_EQ(shape.pitchSlots[1].player, pid(2));
    EXPECT_EQ(swapPlaces(shape, 3, 3), LineupEdit::Unchanged);
    shape.pitchSlots[5].player.reset();
    EXPECT_EQ(swapPlaces(shape, 5, 6), LineupEdit::InvalidSlot);
    EXPECT_EQ(swapPlaces(shape, 1, 11), LineupEdit::InvalidSlot);
}

TEST(LiveShapeTest, MoveLiveSlotIntoAnEmptyGoal) {
    Lineup shape = lineup442();
    EXPECT_EQ(moveLiveSlot(shape, 1, Position::Gk), LineupEdit::GoalkeeperTaken);
    shape.pitchSlots[0].player.reset();
    EXPECT_FALSE(hasGoalkeeperOnPitch(shape));
    EXPECT_EQ(moveLiveSlot(shape, 1, Position::Gk), LineupEdit::Done);
    EXPECT_TRUE(hasGoalkeeperOnPitch(shape));
    EXPECT_EQ(shape.pitchSlots[1].position, Position::Gk);
    // The empty slot of the sent-off goalkeeper takes the right-back place.
    EXPECT_EQ(shape.pitchSlots[0].position, Position::Rb);
    EXPECT_EQ(moveLiveSlot(shape, 0, Position::Cm), LineupEdit::InvalidSlot);
}

TEST(LiveShapeTest, MoveLiveSlotOutOfGoalLeavesNoGoalkeeper) {
    Lineup shape = lineup442();
    EXPECT_EQ(moveLiveSlot(shape, 0, Position::Cb, 0.5, 0.8), LineupEdit::Done);
    EXPECT_FALSE(hasGoalkeeperOnPitch(shape));
    EXPECT_EQ(shape.formation, "5-4-2");
    EXPECT_TRUE(isCustomLiveShape(shape));
}

TEST(LiveShapeTest, LiveFormationNameCountsTheOccupiedSlots) {
    Lineup shape = lineup442();
    EXPECT_EQ(liveFormationName(shape), "4-4-2");
    EXPECT_FALSE(isCustomLiveShape(shape));
    shape.pitchSlots[9].player.reset();
    EXPECT_EQ(liveFormationName(shape), "4-4-1");
    EXPECT_FALSE(isCustomLiveShape(shape));
}

// The requirement of the packet: the shape at a minute across substitutions, a red card and two
// tactic changes.
TEST(LiveShapeTest, ShapeAtAcrossSubsRedCardAndTwoChanges) {
    const Lineup lineup = lineup442();
    const Lineup at30 = changeShape(lineup, *findFormation("4-2-3-1"));
    std::vector<MatchEventDraft> events{sub(60, 12, 10), red(70, 3)};
    std::vector<MatchTacticChangeDraft> changes{changeAt(30, at30)};

    Lineup shape = shapeOrFail(lineup, events, changes, MatchTime{20});
    EXPECT_EQ(shape.formation, "4-4-2");
    EXPECT_EQ(slotOf(shape, 10).position, Position::St);
    EXPECT_TRUE(shape.bench.empty());

    shape = shapeOrFail(lineup, events, changes, MatchTime{45});
    EXPECT_EQ(shape.formation, "4-2-3-1");
    EXPECT_EQ(slotOf(shape, 11).position, Position::Am);

    // The substitute takes the slot (and the place) of the striker.
    shape = shapeOrFail(lineup, events, changes, MatchTime{65});
    EXPECT_EQ(shape.pitchSlots[9].player, pid(12));
    EXPECT_EQ(shape.pitchSlots[9].position, Position::St);

    // The red card empties the slot of the centre back.
    shape = shapeOrFail(lineup, events, changes, MatchTime{72});
    EXPECT_FALSE(shape.pitchSlots[2].player);
    EXPECT_EQ(shape.formation, "3-2-2-1-1");

    // The second change: back to 4-4-2 with ten.
    changes.push_back(changeAt(75, changeShape(shape, *findFormation("4-4-2"))));
    shape = shapeOrFail(lineup, events, changes, MatchTime{80});
    EXPECT_EQ(shape.formation, "4-4-1");
    EXPECT_EQ(slotOf(shape, 7).position, Position::Cb);
    EXPECT_EQ(slotOf(shape, 12).position, Position::St);
    // After everything (no time): the same.
    EXPECT_EQ(shapeOrFail(lineup, events, changes, std::nullopt), shape);

    const auto names = shapeChangeNames(lineup, events, changes);
    ASSERT_TRUE(names.has_value());
    ASSERT_EQ(names->size(), 2U);
    EXPECT_EQ((*names)[0], (ShapeChangeNames{"4-4-2", "4-2-3-1"}));
    EXPECT_EQ((*names)[1], (ShapeChangeNames{"3-2-2-1-1", "4-4-1"}));
}

TEST(LiveShapeTest, SubstitutionEnteredLateBeforeAChangeTakesTheNewPlace) {
    const Lineup lineup = lineup442();
    const std::vector<MatchTacticChangeDraft> changes{
        changeAt(30, changeShape(lineup, *findFormation("4-2-3-1")))};
    // Entered after the change, at an earlier minute: player 14 replaces the central midfielder 8.
    const std::vector<MatchEventDraft> events{sub(20, 14, 8)};
    const Lineup shape = shapeOrFail(lineup, events, changes, MatchTime{45});
    EXPECT_EQ(shape.pitchSlots[7].player, pid(14));
    EXPECT_EQ(shape.pitchSlots[7].position, Position::Dm);
}

TEST(LiveShapeTest, SubstitutionAndChangeAtTheSameMinuteCommute) {
    const Lineup lineup = lineup442();
    const Lineup changed = changeShape(lineup, *findFormation("4-3-3"));
    const std::vector<MatchEventDraft> events{sub(60, 12, 11)};
    const std::vector<MatchTacticChangeDraft> changes{changeAt(60, changed)};
    const Lineup at60 = shapeOrFail(lineup, events, changes, MatchTime{60});
    const Lineup at59 = shapeOrFail(lineup, events, changes, MatchTime{59});
    EXPECT_EQ(at60.formation, "4-3-3");
    EXPECT_EQ(at60.pitchSlots[10].player, pid(12));
    EXPECT_EQ(at59.formation, "4-4-2");
    EXPECT_EQ(at59.pitchSlots[10].player, pid(11));
}

TEST(LiveShapeTest, AddedTimeOfAPartComesBeforeTheNextMinute) {
    const Lineup lineup = lineup442();
    const std::vector<MatchTacticChangeDraft> changes{
        {.minute = 45, .addedTime = 2, .shape = changeShape(lineup, *findFormation("4-3-3"))}};
    EXPECT_EQ(shapeOrFail(lineup, {}, changes, MatchTime{45, 1}).formation, "4-4-2");
    EXPECT_EQ(shapeOrFail(lineup, {}, changes, MatchTime{45, 2}).formation, "4-3-3");
    EXPECT_EQ(shapeOrFail(lineup, {}, changes, MatchTime{45}).formation, "4-3-3");
}

TEST(LiveShapeTest, EventsThatDoNotFitTheLineupAreAnError) {
    const Lineup lineup = lineup442();
    const std::vector<MatchEventDraft> events{sub(10, 12, 2), sub(20, 13, 2)};
    const auto shape = shapeAt(lineup, events, {}, std::nullopt);
    ASSERT_FALSE(shape.has_value());
    EXPECT_EQ(shape.error().index, 1U);
    EXPECT_EQ(shape.error().problem, MatchEventProblem::AlreadySubstituted);
}

TEST(LiveShapeTest, SubstituteGetsThePositionOfHisSlotWithTheChanges) {
    const Lineup lineup = lineup442();
    const std::vector<MatchTacticChangeDraft> changes{
        changeAt(30, changeShape(lineup, *findFormation("4-2-3-1")))};
    const std::vector<MatchEventDraft> events{sub(20, 12, 7), sub(60, 13, 11)};
    const auto minutes = computeMatchMinutes(lineup, events, changes, 90);
    ASSERT_TRUE(minutes.has_value());
    const auto positionOf = [&](std::int64_t player) -> std::optional<Position> {
        for (const AppearanceDraft& appearance : minutes->appearances) {
            if (appearance.playerId == pid(player)) {
                return appearance.position;
            }
        }
        return std::nullopt;
    };
    // Before the change: the CM of the lineup; after it: the AM of the new shape.
    EXPECT_EQ(positionOf(12), Position::Cm);
    EXPECT_EQ(positionOf(13), Position::Am);
    // Starters keep the lineup position; the plain overload gives the old position.
    EXPECT_EQ(positionOf(11), Position::St);
    const auto plain = computeMatchMinutes(matchSheetOf(lineup), events, 90);
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->appearances.back().position, Position::St);
}

// P13b review: the position of a substitute is the place of his slot right after he came on, not
// after the later events of the same minute.
TEST(LiveShapeTest, ASubstituteSentOffInTheMinuteHeCameOnKeepsThePlaceHeTook) {
    const Lineup lineup = lineup442();
    const std::vector<MatchTacticChangeDraft> changes{
        changeAt(30, changeShape(lineup, *findFormation("4-2-3-1")))};
    // Player 11 is the AM of 4-2-3-1; 13 replaces him at 60' and is sent off at 60'.
    const std::vector<MatchEventDraft> events{sub(60, 13, 11), red(60, 13)};
    const auto minutes = computeMatchMinutes(lineup, events, changes, 90);
    ASSERT_TRUE(minutes.has_value());
    const auto substitute =
        std::ranges::find(minutes->appearances, pid(13), &AppearanceDraft::playerId);
    ASSERT_NE(substitute, minutes->appearances.end());
    EXPECT_EQ(substitute->position, Position::Am);
}

TEST(LiveShapeTest, ASubstituteReplacedInTheMinuteHeCameOnKeepsThePlaceHeTook) {
    const Lineup lineup = lineup442();
    const std::vector<MatchTacticChangeDraft> changes{
        changeAt(30, changeShape(lineup, *findFormation("4-2-3-1")))};
    const std::vector<MatchEventDraft> events{sub(60, 13, 11), sub(60, 14, 13)};
    const auto minutes = computeMatchMinutes(lineup, events, changes, 90);
    ASSERT_TRUE(minutes.has_value());
    for (const std::int64_t player : {13, 14}) {
        const auto found =
            std::ranges::find(minutes->appearances, pid(player), &AppearanceDraft::playerId);
        ASSERT_NE(found, minutes->appearances.end()) << player;
        EXPECT_EQ(found->position, Position::Am) << player;
    }
}

// The latest stored action in the order of the match, over events, notes and tactic changes.
TEST(LiveShapeTest, TheLatestRecordedTimeCoversEventsNotesAndChanges) {
    const Lineup lineup = lineup442();
    EXPECT_EQ(latestRecordedTime({}, {}, {}), std::nullopt);
    MatchEventDraft stoppage = red(45, 3);
    stoppage.addedTime = 2;
    const std::vector<MatchEventDraft> events{red(20, 2), stoppage};
    EXPECT_EQ(latestRecordedTime(events, {}, {}), (MatchTime{45, 2}));
    // A note at 46' (stored 45, no added time) is after 45+2'.
    const std::vector<MatchNoteDraft> notes{{45, std::nullopt, std::nullopt, "x"}};
    EXPECT_EQ(latestRecordedTime(events, notes, {}), (MatchTime{45, std::nullopt}));
    // A tactic change at 60' is the latest.
    const std::vector<MatchTacticChangeDraft> changes{
        changeAt(59, changeShape(lineup, *findFormation("4-3-3")))};
    EXPECT_EQ(latestRecordedTime(events, notes, changes), (MatchTime{59, std::nullopt}));
    EXPECT_EQ(latestRecordedTime({}, {}, changes), (MatchTime{59, std::nullopt}));
}
