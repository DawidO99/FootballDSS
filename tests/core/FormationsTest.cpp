#include "core/algorithms/Formations.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace fdss::core;

// The coordinates below are computed by hand from the zone table in Formations.cpp: rows (y of the
// tokens) GK 0.94, defence 0.80, LWB/DM/RWB 0.64, LM/CM/RM 0.48, LW/AM/RW 0.32, ST 0.12; side zones
// 0–0.25 and 0.75–1, the centre 0.25–0.75; n tokens of a zone sit at left + width · (2i + 1) / 2n.

namespace {

Lineup lineupOf(const char* name) { return emptyLineup(*findFormation(name)); }

// Players 1..11 in the slots in order.
Lineup filled(const char* name) {
    Lineup lineup = lineupOf(name);
    for (std::size_t i = 0; i < lineup.pitchSlots.size(); ++i) {
        lineup.pitchSlots[i].player = PlayerId{static_cast<std::int64_t>(i + 1)};
    }
    return lineup;
}

} // namespace

TEST(FormationsTest, CatalogueHasTheSevenFormationsOfThePacket) {
    std::vector<std::string> names;
    for (const Formation& formation : formations()) {
        names.emplace_back(formation.name);
        EXPECT_EQ(formation.positions.front(), Position::Gk) << formation.name;
        EXPECT_EQ(std::ranges::count(formation.positions, Position::Gk), 1) << formation.name;
    }
    EXPECT_EQ(names, (std::vector<std::string>{"4-4-2", "4-3-3", "4-2-3-1", "4-1-4-1", "3-5-2",
                                               "3-4-3", "5-3-2"}));
    EXPECT_EQ(findFormation("4-5-1"), nullptr);
}

TEST(FormationsTest, EveryFormationIsLaidOutInsideThePitch) {
    for (const Formation& formation : formations()) {
        const Lineup lineup = emptyLineup(formation);
        EXPECT_EQ(lineup.formation, formation.name);
        ASSERT_EQ(lineup.pitchSlots.size(), static_cast<std::size_t>(kLineupSlots));
        for (const LineupSlot& slot : lineup.pitchSlots) {
            EXPECT_GT(slot.x, 0.0);
            EXPECT_LT(slot.x, 1.0);
            EXPECT_GT(slot.y, 0.0);
            EXPECT_LT(slot.y, 1.0);
            EXPECT_EQ(positionAt(slot.x, slot.y), slot.position) << formation.name;
            EXPECT_FALSE(slot.player.has_value());
        }
        EXPECT_EQ(matchingFormation(lineup.pitchSlots), &formation);
        EXPECT_EQ(formationName(lineup.pitchSlots), formation.name);
    }
}

TEST(FormationsTest, FourFourTwoCoordinates) {
    const Lineup lineup = lineupOf("4-4-2");
    // gk rb cb cb lb rm cm cm lm st st
    const std::vector<std::pair<double, double>> expected{
        {0.5, 0.94},   {0.875, 0.80}, {0.375, 0.80}, {0.625, 0.80}, {0.125, 0.80}, {0.875, 0.48},
        {0.375, 0.48}, {0.625, 0.48}, {0.125, 0.48}, {0.375, 0.12}, {0.625, 0.12}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_DOUBLE_EQ(lineup.pitchSlots[i].x, expected[i].first) << i;
        EXPECT_DOUBLE_EQ(lineup.pitchSlots[i].y, expected[i].second) << i;
    }
}

TEST(FormationsTest, ZonesCoverThePitchOncePerPosition) {
    std::set<Position> seen;
    for (const PitchZone& zone : pitchZones()) {
        EXPECT_TRUE(seen.insert(zone.position).second);
        EXPECT_EQ(&pitchZone(zone.position), &zone);
        EXPECT_EQ(positionAt(zone.left + zone.width / 2, zone.top + zone.height / 2),
                  zone.position);
    }
    EXPECT_EQ(seen.size(), allValues<Position>().size());

    // Corners and edges.
    EXPECT_EQ(positionAt(0.0, 1.0), Position::Gk);
    EXPECT_EQ(positionAt(1.0, 1.0), Position::Gk);
    EXPECT_EQ(positionAt(0.0, 0.0), Position::St);
    EXPECT_EQ(positionAt(1.0, 0.8), Position::Rb);
    EXPECT_EQ(positionAt(0.25, 0.72), Position::Cb);
    EXPECT_EQ(positionAt(0.2499, 0.5), Position::Lm);
    EXPECT_EQ(positionAt(0.75, 0.3), Position::Rw);
    EXPECT_EQ(positionAt(0.5, 0.2399), Position::St);
    EXPECT_EQ(positionAt(0.5, 0.24), Position::Am);
    EXPECT_EQ(positionAt(0.5, 0.65), Position::Dm);
    // Outside the pitch: the nearest edge.
    EXPECT_EQ(positionAt(-3.0, 2.0), Position::Gk);
    EXPECT_EQ(positionAt(5.0, 0.6), Position::Rwb);
}

TEST(FormationsTest, RowsFromDefenceToAttack) {
    EXPECT_EQ(pitchRow(Position::Gk), 0);
    EXPECT_EQ(pitchRow(Position::Rb), 1);
    EXPECT_EQ(pitchRow(Position::Lwb), 2);
    EXPECT_EQ(pitchRow(Position::Cm), 3);
    EXPECT_EQ(pitchRow(Position::Am), 4);
    EXPECT_EQ(pitchRow(Position::St), 5);

    EXPECT_EQ(rowsName(lineupOf("4-4-2").pitchSlots), "4-4-2");
    // rm and lm are in the midfield row, am in front of it.
    EXPECT_EQ(rowsName(lineupOf("4-2-3-1").pitchSlots), "4-2-2-1-1");
    EXPECT_EQ(rowsName(lineupOf("4-3-3").pitchSlots), "4-1-2-2-1");
    EXPECT_EQ(rowsName(lineupOf("5-3-2").pitchSlots), "3-2-3-2");
}

TEST(FormationsTest, MovingASlotToAnotherZoneChangesItsPosition) {
    Lineup lineup = filled("4-4-2");
    // The left midfielder (slot 8) becomes a left winger.
    ASSERT_EQ(moveSlot(lineup, 8, Position::Lw), LineupEdit::Done);
    EXPECT_EQ(lineup.pitchSlots[8].position, Position::Lw);
    EXPECT_EQ(lineup.pitchSlots[8].player, PlayerId{9});
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[8].x, 0.125);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[8].y, 0.32);
    // The midfield row keeps its places.
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[6].x, 0.375);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[7].x, 0.625);
    // Not a catalogue shape any more: the name is counted from the rows.
    EXPECT_EQ(matchingFormation(lineup.pitchSlots), nullptr);
    EXPECT_EQ(lineup.formation, "4-3-1-2");

    // Back again: the catalogue name returns.
    ASSERT_EQ(moveSlot(lineup, 8, Position::Lm), LineupEdit::Done);
    EXPECT_EQ(lineup.formation, "4-4-2");
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[8].x, 0.125);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[8].y, 0.48);
}

TEST(FormationsTest, TokensOfOneZoneAreSpreadInTheirOrder) {
    Lineup lineup = filled("4-4-2");
    // A third centre back, dropped between the two.
    ASSERT_EQ(moveSlot(lineup, 6, Position::Cb, 0.5), LineupEdit::Done);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[2].x, 0.25 + 0.5 / 6);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[6].x, 0.5);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[3].x, 0.25 + 0.5 * 5 / 6);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[6].y, 0.80);
    // The centre midfielder left alone in the middle.
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[7].x, 0.5);

    // Dropped further right within its own zone: the order changes.
    ASSERT_EQ(moveSlot(lineup, 2, Position::Cb, 0.74), LineupEdit::Done);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[6].x, 0.25 + 0.5 / 6);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[3].x, 0.5);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[2].x, 0.25 + 0.5 * 5 / 6);

    // The same place again changes nothing.
    EXPECT_EQ(moveSlot(lineup, 2, Position::Cb), LineupEdit::Unchanged);
}

TEST(FormationsTest, AtMostOneGoalkeeper) {
    Lineup lineup = filled("4-4-2");
    const Lineup before = lineup;
    EXPECT_EQ(moveSlot(lineup, 2, Position::Gk), LineupEdit::GoalkeeperTaken);
    EXPECT_EQ(lineup, before);

    // The goalkeeper may leave the zone; then another slot may enter it.
    ASSERT_EQ(moveSlot(lineup, 0, Position::Cb), LineupEdit::Done);
    EXPECT_EQ(std::ranges::count(lineup.pitchSlots, Position::Gk, &LineupSlot::position), 0);
    EXPECT_EQ(lineup.formation, "5-4-2");
    ASSERT_EQ(moveSlot(lineup, 9, Position::Gk), LineupEdit::Done);
    EXPECT_EQ(std::ranges::count(lineup.pitchSlots, Position::Gk, &LineupSlot::position), 1);
    EXPECT_EQ(moveSlot(lineup, 0, Position::Gk), LineupEdit::GoalkeeperTaken);
    EXPECT_EQ(moveSlot(lineup, 11, Position::Gk), LineupEdit::InvalidSlot);
}

TEST(FormationsTest, ChangeFormationKeepsPlayersWhosePositionExists) {
    // gk 1, rb 2, cb 3, cb 4, lb 5, rm 6, cm 7, cm 8, lm 9, st 10, st 11
    const Lineup before = filled("4-4-2");
    // 4-3-3: gk rb cb cb lb cm dm cm rw st lw
    const FormationChange change = changeFormation(before, *findFormation("4-3-3"));
    const std::vector<std::optional<PlayerId>> expected{
        PlayerId{1},  PlayerId{2}, PlayerId{3},  PlayerId{4},  PlayerId{5}, PlayerId{7},
        std::nullopt, PlayerId{8}, std::nullopt, PlayerId{10}, std::nullopt};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(change.lineup.pitchSlots[i].player, expected[i]) << i;
    }
    EXPECT_EQ(change.lineup.formation, "4-3-3");
    EXPECT_EQ(change.movedToBench, (std::vector{PlayerId{6}, PlayerId{9}, PlayerId{11}}));
    EXPECT_EQ(change.lineup.bench, change.movedToBench);
    EXPECT_TRUE(change.leftLineup.empty());
}

TEST(FormationsTest, ChangeFormationKeepsTheLeftToRightOrder) {
    Lineup before = filled("4-4-2");
    // The centre backs swap places on the pitch, not their players: 3 is now on the right.
    ASSERT_EQ(moveSlot(before, 2, Position::Cb, 0.74), LineupEdit::Done);
    const FormationChange change = changeFormation(before, *findFormation("4-1-4-1"));
    EXPECT_EQ(change.lineup.pitchSlots[2].player, PlayerId{4}); // left centre back
    EXPECT_EQ(change.lineup.pitchSlots[3].player, PlayerId{3});
}

TEST(FormationsTest, ChangeFormationWithAFullBench) {
    Lineup before = filled("4-4-2");
    for (std::int64_t id = 20; id < 31; ++id) {
        before.bench.push_back(PlayerId{id});
    }
    ASSERT_EQ(before.bench.size(), 11U);
    const FormationChange change = changeFormation(before, *findFormation("4-3-3"));
    EXPECT_EQ(change.movedToBench, (std::vector{PlayerId{6}}));
    EXPECT_EQ(change.leftLineup, (std::vector{PlayerId{9}, PlayerId{11}}));
    EXPECT_EQ(std::ssize(change.lineup.bench), kMaxBench);
}

TEST(FormationsTest, ChangeFormationFromACustomShape) {
    Lineup before = filled("4-4-2");
    ASSERT_EQ(moveSlot(before, 8, Position::Lw), LineupEdit::Done); // player 9 on the left wing
    const FormationChange change = changeFormation(before, *findFormation("4-3-3"));
    EXPECT_EQ(change.lineup.pitchSlots[10].position, Position::Lw);
    EXPECT_EQ(change.lineup.pitchSlots[10].player, PlayerId{9});
    EXPECT_EQ(change.movedToBench, (std::vector{PlayerId{6}, PlayerId{11}}));
}

// Codex review of P12: several tokens of one zone never overlap. A side zone holds one token in a
// row, so three left wingers stand in three rows 0.14 apart around the y of the zone (0.32).
TEST(FormationsTest, ThreeTokensOfASideZoneStandInRows) {
    Lineup lineup = filled("4-4-2");
    // The left midfielder and both strikers become left wingers.
    ASSERT_EQ(moveSlot(lineup, 8, Position::Lw), LineupEdit::Done);
    ASSERT_EQ(moveSlot(lineup, 9, Position::Lw), LineupEdit::Done);
    ASSERT_EQ(moveSlot(lineup, 10, Position::Lw), LineupEdit::Done);
    std::vector<double> ys;
    for (const int slot : {8, 9, 10}) {
        EXPECT_DOUBLE_EQ(lineup.pitchSlots[static_cast<std::size_t>(slot)].x, 0.125);
        ys.push_back(lineup.pitchSlots[static_cast<std::size_t>(slot)].y);
        EXPECT_EQ(positionAt(0.125, 0.32), Position::Lw);
    }
    std::ranges::sort(ys);
    EXPECT_NEAR(ys[0], 0.18, 1e-9);
    EXPECT_NEAR(ys[1], 0.32, 1e-9);
    EXPECT_NEAR(ys[2], 0.46, 1e-9);
    EXPECT_EQ(lineup.formation, "4-3-3");
}

TEST(FormationsTest, AWideZoneFillsRowsOfThree) {
    Lineup lineup = filled("4-4-2");
    // Four centre midfielders: three in the first row, one below.
    ASSERT_EQ(moveSlot(lineup, 5, Position::Cm), LineupEdit::Done);
    ASSERT_EQ(moveSlot(lineup, 8, Position::Cm, 0.5, 0.9), LineupEdit::Done);
    // Slots 5, 6, 7 in the first row (y 0.41), slot 8 dropped low in the second (y 0.55).
    for (const int slot : {5, 6, 7}) {
        EXPECT_NEAR(lineup.pitchSlots[static_cast<std::size_t>(slot)].y, 0.41, 1e-9) << slot;
    }
    EXPECT_NEAR(lineup.pitchSlots[8].y, 0.55, 1e-9);
    EXPECT_DOUBLE_EQ(lineup.pitchSlots[8].x, 0.5);
    // The first row is spread over the zone left to right.
    std::vector<double> xs{lineup.pitchSlots[5].x, lineup.pitchSlots[6].x, lineup.pitchSlots[7].x};
    std::ranges::sort(xs);
    EXPECT_DOUBLE_EQ(xs[0], 0.25 + 0.5 / 6);
    EXPECT_DOUBLE_EQ(xs[1], 0.5);
    EXPECT_DOUBLE_EQ(xs[2], 0.25 + 0.5 * 5 / 6);
}
