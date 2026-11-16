#include "core/algorithms/LineupValidator.hpp"
#include "core/algorithms/Formations.hpp"
#include "core/algorithms/LineupRules.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

using namespace fdss::core;

namespace {

// 4-4-2 with players 1..11 in the slots (gk rb cb cb lb rm cm cm lm st st); everyone available and
// in a nominal position. Bench players 21.. play centre midfield.
struct Fixture {
    Lineup lineup = emptyLineup(*findFormation("4-4-2"));
    LineupContext context;

    Fixture() {
        for (std::size_t i = 0; i < lineup.pitchSlots.size(); ++i) {
            const PlayerId id{static_cast<std::int64_t>(i + 1)};
            lineup.pitchSlots[i].player = id;
            context.players[id] =
                LineupPlayerFacts{.primaryPosition = lineup.pitchSlots[i].position};
        }
    }

    PlayerId addBench(std::int64_t id) {
        lineup.bench.push_back(PlayerId{id});
        context.players[PlayerId{id}] = LineupPlayerFacts{.primaryPosition = Position::Cm};
        return PlayerId{id};
    }

    LineupPlayerFacts& facts(std::int64_t id) { return context.players.at(PlayerId{id}); }

    [[nodiscard]] std::vector<LineupIssue> validate() const {
        return LineupValidator().validate(lineup, context);
    }
};

std::vector<LineupRule> rulesOf(const std::vector<LineupIssue>& issues) {
    std::vector<LineupRule> rules;
    for (const LineupIssue& issue : issues) {
        rules.push_back(issue.rule);
    }
    return rules;
}

} // namespace

TEST(LineupValidatorTest, AFullAvailableLineupHasNoIssues) {
    Fixture f;
    f.addBench(21);
    EXPECT_TRUE(f.validate().empty());
    EXPECT_FALSE(hasErrors(f.validate()));
}

TEST(LineupValidatorTest, StartingElevenCountsThePlayersInTheSlots) {
    Fixture f;
    f.lineup.pitchSlots[5].player.reset();
    const auto issues = f.validate();
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, LineupRule::StartingEleven);
    EXPECT_EQ(issues[0].severity, LineupSeverity::Error);
    EXPECT_EQ(issues[0].count, 10);
    EXPECT_EQ(issues[0].player, std::nullopt);
    EXPECT_TRUE(hasErrors(issues));
}

TEST(LineupValidatorTest, AnEmptyGoalkeeperSlotIsAnError) {
    Fixture f;
    f.lineup.pitchSlots[0].player.reset();
    EXPECT_EQ(rulesOf(f.validate()),
              (std::vector{LineupRule::StartingEleven, LineupRule::Goalkeeper}));
}

TEST(LineupValidatorTest, NoGoalkeeperPositionAtAllIsAnError) {
    Fixture f;
    ASSERT_EQ(moveSlot(f.lineup, 0, Position::Cb), LineupEdit::Done);
    // Player 1 is a goalkeeper in a centre back slot: out of position too.
    EXPECT_EQ(rulesOf(f.validate()),
              (std::vector{LineupRule::Goalkeeper, LineupRule::OutOfPosition}));
}

TEST(LineupValidatorTest, AnOutfieldPlayerInGoalIsOnlyAWarning) {
    Fixture f;
    ASSERT_EQ(swapSlots(f.lineup, 0, 9), LineupEdit::Done); // striker 10 in goal
    const auto issues = f.validate();
    EXPECT_FALSE(hasErrors(issues));
    ASSERT_EQ(issues.size(), 2U);
    EXPECT_EQ(issues[0].rule, LineupRule::OutOfPosition);
    EXPECT_EQ(issues[0].player, PlayerId{10});
    EXPECT_EQ(issues[0].slot, 0);
    EXPECT_EQ(issues[0].slotPosition, Position::Gk);
    EXPECT_EQ(issues[1].player, PlayerId{1});
    EXPECT_EQ(issues[1].slotPosition, Position::St);
}

TEST(LineupValidatorTest, EveryUnavailableStatusIsAnError) {
    for (const PlayerStatus status : {PlayerStatus::Injured, PlayerStatus::Ill,
                                      PlayerStatus::Suspended, PlayerStatus::Absent}) {
        Fixture f;
        f.facts(4).statusOnMatchDay = status;
        const auto issues = f.validate();
        ASSERT_EQ(issues.size(), 1U);
        EXPECT_EQ(issues[0].rule, LineupRule::Unavailable);
        EXPECT_EQ(issues[0].severity, LineupSeverity::Error);
        EXPECT_EQ(issues[0].player, PlayerId{4});
        EXPECT_EQ(issues[0].slot, 3);
        EXPECT_EQ(issues[0].status, status);
    }
}

TEST(LineupValidatorTest, AnUnavailablePlayerOnTheBenchIsAnError) {
    Fixture f;
    f.addBench(21);
    const PlayerId injured = f.addBench(22);
    f.facts(22).statusOnMatchDay = PlayerStatus::Injured;
    const auto issues = f.validate();
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].player, injured);
    EXPECT_EQ(issues[0].slot, std::nullopt);
    EXPECT_EQ(issues[0].benchIndex, 1);
}

TEST(LineupValidatorTest, ADuplicateIsReportedWhereItRepeats) {
    Fixture f;
    f.lineup.bench.push_back(PlayerId{7});
    const auto issues = f.validate();
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, LineupRule::Duplicate);
    EXPECT_EQ(issues[0].player, PlayerId{7});
    EXPECT_EQ(issues[0].benchIndex, 0);

    Fixture distinct;
    distinct.addBench(21);
    distinct.addBench(22);
    EXPECT_TRUE(distinct.validate().empty());
}

TEST(LineupValidatorTest, HighLoadWarnsOnlyForHigh) {
    Fixture f;
    const PlayerId bench = f.addBench(21);
    f.facts(2).loadStatus = LoadStatus::High;
    f.facts(3).loadStatus = LoadStatus::Elevated;
    f.facts(4).loadStatus = LoadStatus::NotEnoughData;
    f.facts(21).loadStatus = LoadStatus::High;
    const auto issues = f.validate();
    ASSERT_EQ(issues.size(), 2U);
    EXPECT_EQ(issues[0].rule, LineupRule::HighLoad);
    EXPECT_EQ(issues[0].severity, LineupSeverity::Warning);
    EXPECT_EQ(issues[0].player, PlayerId{2});
    EXPECT_EQ(issues[1].player, bench);
}

TEST(LineupValidatorTest, PrimaryAndSecondaryPositionsAreNominal) {
    Fixture f;
    f.facts(3).primaryPosition = Position::Dm;
    f.facts(3).secondaryPositions = {Position::Rb, Position::Cb};
    f.facts(4).primaryPosition = Position::Lb; // in a centre back slot
    f.facts(4).secondaryPositions = {Position::Lwb};
    f.addBench(21);
    f.facts(21).primaryPosition = Position::Gk; // the bench has no positions
    const auto issues = f.validate();
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, LineupRule::OutOfPosition);
    EXPECT_EQ(issues[0].player, PlayerId{4});
    EXPECT_EQ(issues[0].slotPosition, Position::Cb);
}

TEST(LineupValidatorTest, MissedSessions) {
    Fixture f;
    f.facts(9).missedRecentSessions = true;
    const auto issues = f.validate();
    ASSERT_EQ(issues.size(), 1U);
    EXPECT_EQ(issues[0].rule, LineupRule::MissedSessions);
    EXPECT_EQ(issues[0].player, PlayerId{9});
    EXPECT_EQ(issues[0].count, kRecentSessions);
}

// Cards accumulate up to a suspension and the count starts again (P12 decision 3).
TEST(LineupValidatorTest, YellowCardsOneBeforeEverySuspension) {
    struct Case {
        int cards;
        bool warns;
    };
    for (const Case c : {Case{0, false}, Case{2, false}, Case{3, true}, Case{4, false},
                         Case{5, false}, Case{7, true}, Case{8, false}, Case{11, true}}) {
        Fixture f;
        f.context.yellowCardThreshold = 4;
        f.facts(6).yellowCards = c.cards;
        const auto issues = f.validate();
        ASSERT_EQ(issues.size(), c.warns ? 1U : 0U) << c.cards;
        if (c.warns) {
            EXPECT_EQ(issues[0].rule, LineupRule::YellowCards);
            EXPECT_EQ(issues[0].player, PlayerId{6});
            EXPECT_EQ(issues[0].count, c.cards);
        }
    }
}

TEST(LineupValidatorTest, YellowCardsWithAnotherThreshold) {
    Fixture f;
    f.context.yellowCardThreshold = 2;
    f.facts(6).yellowCards = 1;
    f.facts(7).yellowCards = 2;
    f.facts(8).yellowCards = 3;
    EXPECT_EQ(f.validate().size(), 2U);

    f.context.yellowCardThreshold = 1; // below the range: no warning at all
    EXPECT_TRUE(f.validate().empty());
}

TEST(LineupValidatorTest, APlayerOutsideTheSquadIsCheckedOnlyForCountsAndDuplicates) {
    Fixture f;
    f.context.players.erase(PlayerId{5});
    EXPECT_TRUE(f.validate().empty());
}

TEST(LineupValidatorTest, ErrorsFirstThenWarningsInPlaceOrder) {
    Fixture f;
    f.lineup.pitchSlots[0].player.reset();                  // StartingEleven, Goalkeeper
    f.facts(10).statusOnMatchDay = PlayerStatus::Suspended; // slot 9
    f.facts(3).statusOnMatchDay = PlayerStatus::Injured;    // slot 2
    f.facts(3).loadStatus = LoadStatus::High;               // warning, slot 2
    f.facts(2).yellowCards = 3;                             // warning, slot 1
    f.addBench(21);
    f.facts(21).missedRecentSessions = true; // warning, bench 0
    const auto issues = f.validate();
    EXPECT_EQ(rulesOf(issues), (std::vector{LineupRule::StartingEleven, LineupRule::Goalkeeper,
                                            LineupRule::Unavailable, LineupRule::Unavailable,
                                            LineupRule::YellowCards, LineupRule::HighLoad,
                                            LineupRule::MissedSessions}));
    EXPECT_EQ(issues[2].slot, 2);
    EXPECT_EQ(issues[3].slot, 9);
}

TEST(LineupValidatorTest, RulesAreStrategies) {
    std::vector<std::unique_ptr<ILineupRule>> rules;
    rules.push_back(std::make_unique<GoalkeeperRule>());
    const LineupValidator onlyGoalkeeper(std::move(rules));
    Fixture f;
    f.lineup.pitchSlots[0].player.reset();
    f.facts(3).statusOnMatchDay = PlayerStatus::Injured;
    EXPECT_EQ(rulesOf(onlyGoalkeeper.validate(f.lineup, f.context)),
              (std::vector{LineupRule::Goalkeeper}));
}
