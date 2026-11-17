#include "viewmodels/MatchDayViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/algorithms/Formations.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/Player.hpp"
#include "services/CalendarService.hpp"
#include "services/LineupService.hpp"
#include "services/SquadService.hpp"

#include <QStringList>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using namespace std::chrono_literals;
using fdss::viewmodels::MatchDayPlayersModel;
using fdss::viewmodels::MatchDayViewModel;

namespace {

// FakeClock: today is Sunday 2026-11-01, 12:00 UTC; the match is today.
constexpr Date kMatchDate{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}};

// Players 1..11 fit 4-4-2 (gk rb cb cb lb rm cm cm lm st st), 12..14 are the bench.
constexpr std::array<Position, 11> kPositions{
    Position::Gk, Position::Rb, Position::Cb, Position::Cb, Position::Lb, Position::Rm,
    Position::Cm, Position::Cm, Position::Lm, Position::St, Position::St};
constexpr int kSquad = 14;

class Messages {
public:
    template <class Signal> Messages(const MatchDayViewModel* vm, Signal signal) {
        QObject::connect(vm, signal, &m_context, [this](const QString& text) { m_texts << text; });
    }
    [[nodiscard]] const QStringList& texts() const { return m_texts; }

private:
    QObject m_context;
    QStringList m_texts;
};

class MatchDayViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
        for (int i = 1; i <= kSquad; ++i) {
            PlayerDraft draft;
            draft.firstName = "Gracz";
            draft.lastName = std::format("Numer{:02}", i);
            draft.shirtNumber = i;
            draft.primaryPosition =
                i <= 11 ? kPositions[static_cast<std::size_t>(i - 1)] : Position::Cm;
            const auto id = registry().squad().addPlayer(m_team, draft);
            ASSERT_TRUE(id.has_value()) << id.error().detail;
            m_players.push_back(*id);
        }
        OpponentDraft opponent;
        opponent.name = "Rywal";
        const auto opponentId = registry().calendar().addOpponent(opponent);
        ASSERT_TRUE(opponentId.has_value());
        MatchDraft match;
        match.opponentId = *opponentId;
        match.date = kMatchDate;
        const auto matchId = registry().calendar().addMatch(m_team, match);
        ASSERT_TRUE(matchId.has_value());
        m_match = *matchId;
    }

    void saveLineup() {
        Lineup lineup = emptyLineup(*findFormation("4-4-2"));
        for (std::size_t slot = 0; slot < lineup.pitchSlots.size(); ++slot) {
            lineup.pitchSlots[slot].player = m_players[slot];
        }
        lineup.bench = {m_players[11], m_players[12], m_players[13]};
        ASSERT_TRUE(registry().lineup().save(m_match, lineup).has_value());
    }

    std::unique_ptr<MatchDayViewModel> open() {
        auto vm = std::make_unique<MatchDayViewModel>();
        vm->setTeamId(m_team.value);
        vm->openDefaultMatch();
        return vm;
    }

    // A live match in the first half, kicked off at 12:00.
    std::unique_ptr<MatchDayViewModel> kickedOff() {
        saveLineup();
        auto vm = open();
        EXPECT_TRUE(vm->start(45));
        EXPECT_TRUE(vm->advanceClock());
        return vm;
    }

    [[nodiscard]] qint64 player(int i) const {
        return m_players[static_cast<std::size_t>(i - 1)].value;
    }

    [[nodiscard]] static int rowOf(const MatchDayPlayersModel& model, qint64 player) {
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.value(row, "playerId").toLongLong() == player) {
                return row;
            }
        }
        return -1;
    }

    [[nodiscard]] static int selectableCount(const MatchDayPlayersModel& model) {
        int count = 0;
        for (int row = 0; row < model.rowCount(); ++row) {
            count += model.value(row, "selectable").toBool() ? 1 : 0;
        }
        return count;
    }

    void logInAs(Role role) {
        const auto id = addStaff("Ktoś Inny", role);
        ASSERT_TRUE(registry().auth().login(id, kPin).has_value());
    }

    TeamId m_team;
    MatchId m_match;
    std::vector<PlayerId> m_players;
};

} // namespace

TEST_F(MatchDayViewModelTest, WithoutALineupTheStartIsBlockedWithAReason) {
    const auto vm = open();
    EXPECT_EQ(vm->matchId(), m_match.value);
    EXPECT_EQ(vm->opponentName(), "Rywal");
    EXPECT_EQ(vm->statusKey(), "planned");
    EXPECT_EQ(vm->startBlock(), "noLineup");
    EXPECT_FALSE(vm->startBlockText().isEmpty());
    EXPECT_FALSE(vm->canStart());
    EXPECT_EQ(vm->defaultHalfLength(), 45);
    EXPECT_FALSE(vm->guardLeaving());

    saveLineup();
    vm->reload();
    EXPECT_EQ(vm->startBlock(), "");
    EXPECT_TRUE(vm->canStart());
}

TEST_F(MatchDayViewModelTest, StartThenTheClockSteps) {
    saveLineup();
    const auto vm = open();
    ASSERT_TRUE(vm->start(40));
    EXPECT_EQ(vm->statusKey(), "live");
    EXPECT_TRUE(vm->guardLeaving());
    EXPECT_EQ(vm->phase(), "notStarted");
    EXPECT_EQ(vm->clockText(), "00:00");
    EXPECT_EQ(vm->clockActionText(), "Start 1st half");
    EXPECT_TRUE(vm->canCancelStart());
    EXPECT_FALSE(vm->canRecordEvents());
    EXPECT_EQ(vm->pitch()->rowCount(), 11);
    EXPECT_EQ(vm->bench()->rowCount(), 3);

    ASSERT_TRUE(vm->advanceClock());
    EXPECT_EQ(vm->phase(), "firstHalf");
    EXPECT_FALSE(vm->canCancelStart());
    EXPECT_TRUE(vm->canRecordEvents());
    m_clock->advance(23min + 41s);
    EXPECT_EQ(vm->clockText(), "23:41");
    m_clock->advance(18min + 29s); // 42:10 with 40-minute halves
    EXPECT_EQ(vm->clockText(), "40+2:10");
    EXPECT_EQ(vm->clockActionText(), "End 1st half");
}

TEST_F(MatchDayViewModelTest, GoalByTapsWithAndWithoutAssist) {
    const auto vm = kickedOff();
    m_clock->advance(10min + 30s);
    vm->beginAction("goal");
    EXPECT_EQ(vm->pendingAction(), "scorer");
    EXPECT_FALSE(vm->pendingPrompt().isEmpty());
    EXPECT_EQ(selectableCount(*vm->pitch()), 11);
    EXPECT_EQ(selectableCount(*vm->bench()), 0);
    ASSERT_TRUE(vm->choosePlayer(player(10)));
    EXPECT_EQ(vm->pendingAction(), "assist");
    EXPECT_EQ(selectableCount(*vm->pitch()), 10);
    EXPECT_FALSE(vm->pitch()->value(rowOf(*vm->pitch(), player(10)), "selectable").toBool());
    ASSERT_TRUE(vm->choosePlayer(player(11)));
    EXPECT_EQ(vm->pendingAction(), "");
    EXPECT_EQ(vm->goalsFor(), 1);
    ASSERT_EQ(vm->timeline()->rowCount(), 1);
    EXPECT_EQ(vm->timeline()->value(0, "minuteText"), "11'");
    EXPECT_EQ(vm->timeline()->value(0, "typeKey"), "goal");
    EXPECT_EQ(vm->timeline()->value(0, "playerText"), "Gracz Numer10");
    EXPECT_EQ(vm->timeline()->value(0, "relatedText"), "Assist: Gracz Numer11");
    EXPECT_EQ(vm->pitch()->value(rowOf(*vm->pitch(), player(10)), "goals"), 1);

    vm->beginAction("goal");
    ASSERT_TRUE(vm->choosePlayer(player(9)));
    ASSERT_TRUE(vm->skipAssist());
    EXPECT_EQ(vm->goalsFor(), 2);
    EXPECT_EQ(vm->timeline()->value(0, "relatedText"), "");

    ASSERT_TRUE(vm->recordOpponentGoal());
    EXPECT_EQ(vm->goalsAgainst(), 1);
    EXPECT_EQ(vm->timeline()->value(0, "typeKey"), "opponentGoal");
    EXPECT_EQ(vm->timeline()->value(0, "playerText"), "Rywal");
}

TEST_F(MatchDayViewModelTest, SubstitutionByTapsMovesThePlayers) {
    const auto vm = kickedOff();
    m_clock->advance(60min); // stoppage time of the 1st half: still allowed
    vm->beginAction("sub");
    EXPECT_EQ(vm->pendingAction(), "subOff");
    ASSERT_TRUE(vm->choosePlayer(player(5)));
    EXPECT_EQ(vm->pendingAction(), "subOn");
    EXPECT_EQ(selectableCount(*vm->pitch()), 0);
    EXPECT_EQ(selectableCount(*vm->bench()), 3);
    ASSERT_TRUE(vm->choosePlayer(player(12)));
    EXPECT_NE(rowOf(*vm->pitch(), player(12)), -1);
    const int off = rowOf(*vm->bench(), player(5));
    ASSERT_NE(off, -1);
    EXPECT_EQ(vm->bench()->value(off, "stateKey"), "substitutedOff");
    EXPECT_EQ(vm->bench()->value(off, "minutesText"), "45'");
    EXPECT_EQ(vm->pitch()->value(rowOf(*vm->pitch(), player(12)), "positionCode"), "LB");
    EXPECT_EQ(vm->timeline()->value(0, "minuteText"), "45+16'");
    EXPECT_EQ(vm->timeline()->value(0, "playerText"), "On: Gracz Numer12");
    EXPECT_EQ(vm->timeline()->value(0, "relatedText"), "Off: Gracz Numer05");
}

TEST_F(MatchDayViewModelTest, SecondYellowAndUndoNameTheAction) {
    const auto vm = kickedOff();
    m_clock->advance(5min);
    vm->beginAction("yellow");
    ASSERT_TRUE(vm->choosePlayer(player(4)));
    m_clock->advance(20min);
    vm->beginAction("yellow");
    ASSERT_TRUE(vm->choosePlayer(player(4)));
    EXPECT_EQ(vm->timeline()->value(0, "typeKey"), "secondYellow");
    const int row = rowOf(*vm->bench(), player(4));
    ASSERT_NE(row, -1);
    EXPECT_EQ(vm->bench()->value(row, "stateKey"), "sentOff");
    EXPECT_EQ(vm->bench()->value(row, "yellowCards"), 2);
    EXPECT_TRUE(vm->canUndo());
    EXPECT_EQ(vm->undoText(), "Second yellow, red card · Gracz Numer04 26'");

    EXPECT_EQ(vm->undo(), "Undone: Second yellow, red card · Gracz Numer04 26'");
    EXPECT_NE(rowOf(*vm->pitch(), player(4)), -1);
    EXPECT_EQ(vm->timeline()->rowCount(), 1);
    EXPECT_EQ(vm->undo(), "Undone: Yellow card · Gracz Numer04 6'");
    EXPECT_EQ(vm->undo(), "Undone: start of the 1st half");
    EXPECT_FALSE(vm->canUndo());
    EXPECT_EQ(vm->undoText(), "");
}

TEST_F(MatchDayViewModelTest, OtherRolesSeeTheLiveMatchReadOnly) {
    const auto vm = kickedOff();
    logInAs(Role::Physio);
    const auto viewer = open();
    EXPECT_EQ(viewer->matchId(), m_match.value);
    EXPECT_EQ(viewer->statusKey(), "live");
    EXPECT_TRUE(viewer->readOnly());
    EXPECT_FALSE(viewer->readOnlyText().isEmpty());
    EXPECT_FALSE(viewer->guardLeaving());
    EXPECT_FALSE(viewer->canRecordEvents());
    EXPECT_FALSE(viewer->canUndo());
    viewer->beginAction("goal");
    EXPECT_EQ(viewer->pendingAction(), "");
    Messages errors(viewer.get(), &MatchDayViewModel::errorOccurred);
    EXPECT_FALSE(viewer->advanceClock());
    EXPECT_EQ(errors.texts().size(), 1);
}

TEST_F(MatchDayViewModelTest, ARefusedActionSaysWhy) {
    const auto vm = kickedOff();
    m_clock->advance(10min);
    Messages errors(vm.get(), &MatchDayViewModel::errorOccurred);
    vm->beginAction("sub");
    ASSERT_TRUE(vm->choosePlayer(player(5)));
    // A player on the pitch cannot come on.
    EXPECT_FALSE(vm->choosePlayer(player(6)));
    ASSERT_EQ(errors.texts().size(), 1);
    EXPECT_EQ(errors.texts().front(),
              "This player is already on the pitch. Choose a player from the bench.");
}

TEST_F(MatchDayViewModelTest, EndOfTheMatchAndAFinishedMatch) {
    const auto vm = kickedOff();
    m_clock->advance(46min);
    ASSERT_TRUE(vm->advanceClock());
    EXPECT_EQ(vm->phase(), "halfTime");
    EXPECT_FALSE(vm->canRecordEvents());
    EXPECT_TRUE(vm->canSubstitute());
    m_clock->advance(15min);
    ASSERT_TRUE(vm->advanceClock());
    m_clock->advance(47min);
    EXPECT_EQ(vm->clockText(), "90+2:00");
    EXPECT_EQ(vm->clockActionText(), "End match");
    ASSERT_TRUE(vm->advanceClock());
    EXPECT_EQ(vm->statusKey(), "finished");
    EXPECT_EQ(vm->phase(), "finished");
    EXPECT_EQ(vm->clockText(), "90:00");
    EXPECT_FALSE(vm->guardLeaving());
    EXPECT_FALSE(vm->canAdvanceClock());
    EXPECT_EQ(vm->pitch()->value(0, "minutesText"), "90'");
    // The end can still be undone.
    EXPECT_TRUE(vm->canUndo());
}

TEST_F(MatchDayViewModelTest, TheLiveMatchOpensFirst) {
    saveLineup();
    MatchDraft earlier;
    earlier.opponentId = registry().calendar().listOpponents()->front().id;
    earlier.date = kMatchDate;
    ASSERT_TRUE(registry().calendar().addMatch(m_team, earlier).has_value());
    {
        auto vm = std::make_unique<MatchDayViewModel>();
        vm->openMatch(m_match.value);
        ASSERT_TRUE(vm->start(45));
    }
    const auto vm = open();
    EXPECT_EQ(vm->matchId(), m_match.value);
}
