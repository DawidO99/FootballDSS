#include "viewmodels/MatchDayViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/algorithms/Formations.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/Player.hpp"
#include "data/db/DatabaseManager.hpp"
#include "services/CalendarService.hpp"
#include "services/LineupService.hpp"
#include "services/MatchDayService.hpp"
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
        EXPECT_TRUE(vm->start("2x45", 0, 0));
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
    EXPECT_EQ(vm->defaultFormat(), "2x45");
    EXPECT_EQ(vm->formatOptions().size(), 8);
    EXPECT_FALSE(vm->guardLeaving());

    saveLineup();
    vm->reload();
    EXPECT_EQ(vm->startBlock(), "");
    EXPECT_TRUE(vm->canStart());
}

TEST_F(MatchDayViewModelTest, StartThenTheClockSteps) {
    saveLineup();
    const auto vm = open();
    ASSERT_TRUE(vm->start("2x40", 0, 0));
    EXPECT_EQ(vm->formatText(), "2 × 40 min");
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
    EXPECT_EQ(vm->phase(), "running");
    EXPECT_EQ(vm->phaseText(), "1st half");
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

    vm->beginAction("opponentGoal");
    EXPECT_EQ(vm->pendingAction(), "opponentGoal");
    EXPECT_EQ(selectableCount(*vm->pitch()), 0);
    ASSERT_TRUE(vm->confirmAction());
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
    EXPECT_EQ(vm->phase(), "break");
    EXPECT_EQ(vm->phaseText(), "Half time");
    // Late entries in the break: a goal at the last minute played, a substitution at 46'.
    EXPECT_TRUE(vm->canRecordEvents());
    EXPECT_TRUE(vm->canSubstitute());
    vm->beginAction("goal");
    EXPECT_EQ(vm->pendingMinute(), "45+2"); // the half ended at 46:00
    vm->beginAction("sub");
    EXPECT_EQ(vm->pendingMinute(), "46");
    vm->cancelAction();
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
        ASSERT_TRUE(vm->start("2x45", 0, 0));
    }
    const auto vm = open();
    EXPECT_EQ(vm->matchId(), m_match.value);
}

// --- P13 review ---

TEST_F(MatchDayViewModelTest, AYellowThenAStraightRedIsARedCard) {
    const auto vm = kickedOff();
    m_clock->advance(10min);
    vm->beginAction("yellow");
    ASSERT_TRUE(vm->choosePlayer(player(4)));
    vm->beginAction("red");
    ASSERT_TRUE(vm->choosePlayer(player(4)));
    EXPECT_EQ(vm->timeline()->value(0, "typeKey"), "red");
    EXPECT_EQ(vm->timeline()->value(0, "typeText"), "Red card");
    EXPECT_EQ(vm->undoText(), "Red card · Gracz Numer04 11'");
}

TEST_F(MatchDayViewModelTest, AnInconsistentHistoryIsShownAsAnError) {
    const auto vm = kickedOff();
    // Player 1 starts, so he cannot come on.
    ASSERT_TRUE(m_db->execSql(QStringLiteral("INSERT INTO match_event (match_id, minute, type, "
                                             "player_id, related_player_id) VALUES (%1, 5, 'sub', "
                                             "%2, %3)")
                                  .arg(m_match.value)
                                  .arg(player(1))
                                  .arg(player(2)),
                              "test")
                    .has_value());
    vm->reload();
    EXPECT_FALSE(vm->hasMatch());
    EXPECT_EQ(vm->loadError().left(43), "The history of this match is inconsistent: ");
    EXPECT_EQ(vm->pitch()->rowCount(), 0);
}

TEST_F(MatchDayViewModelTest, UndoOfTheEndNamesTheOtherLiveMatch) {
    const auto vm = kickedOff();
    m_clock->advance(46min);
    ASSERT_TRUE(vm->advanceClock());
    ASSERT_TRUE(vm->advanceClock());
    m_clock->advance(46min);
    ASSERT_TRUE(vm->advanceClock());
    ASSERT_EQ(vm->statusKey(), "finished");

    OpponentDraft opponent;
    opponent.name = "Inny Rywal";
    MatchDraft other;
    other.opponentId = registry().calendar().addOpponent(opponent).value();
    other.date = kMatchDate;
    const MatchId otherMatch = registry().calendar().addMatch(m_team, other).value();
    Lineup lineup = emptyLineup(*findFormation("4-4-2"));
    for (std::size_t slot = 0; slot < lineup.pitchSlots.size(); ++slot) {
        lineup.pitchSlots[slot].player = m_players[slot];
    }
    ASSERT_TRUE(registry().lineup().save(otherMatch, lineup).has_value());
    ASSERT_TRUE(registry().matchDay().start(otherMatch, {2, 45}, 4).has_value());

    Messages errors(vm.get(), &MatchDayViewModel::errorOccurred);
    EXPECT_EQ(vm->undo(), "");
    ASSERT_EQ(errors.texts().size(), 1);
    EXPECT_EQ(errors.texts().front(),
              "Another match of this team is live. First finish it, or undo "
              "its start, in the match against Inny Rywal.");
    EXPECT_EQ(vm->statusKey(), "finished");
}

// --- Manual test of P13: format, minute field, late entry, notes, minimum of players ---

TEST_F(MatchDayViewModelTest, ThreePartsAndACustomFormat) {
    saveLineup();
    const auto vm = open();
    ASSERT_TRUE(vm->start("3x30", 0, 0));
    EXPECT_EQ(vm->formatText(), "3 × 30 min");
    EXPECT_EQ(vm->clockActionText(), "Start 1st period");
    ASSERT_TRUE(vm->advanceClock());
    m_clock->advance(31min + 10s);
    EXPECT_EQ(vm->phaseText(), "1st period");
    EXPECT_EQ(vm->clockText(), "30+1:10");
    EXPECT_EQ(vm->clockActionText(), "End 1st period");
    ASSERT_TRUE(vm->advanceClock());
    EXPECT_EQ(vm->phaseText(), "Break");
    EXPECT_EQ(vm->clockActionText(), "Start 2nd period");
    EXPECT_EQ(vm->undoText(), "end of the 1st period");

    ASSERT_TRUE(vm->undo().size() > 0);
    ASSERT_TRUE(vm->undo().size() > 0);
    ASSERT_TRUE(vm->cancelStart());
    ASSERT_TRUE(vm->start("custom", 4, 20));
    EXPECT_EQ(vm->formatText(), "4 × 20 min");
    Messages errors(vm.get(), &MatchDayViewModel::errorOccurred);
    ASSERT_TRUE(vm->cancelStart());
    EXPECT_FALSE(vm->start("custom", 4, 40)); // 160 minutes
    EXPECT_EQ(errors.texts().size(), 1);
}

TEST_F(MatchDayViewModelTest, TheMinuteCanBeChangedAndEnteredLate) {
    const auto vm = kickedOff();
    m_clock->advance(20min + 30s);
    vm->beginAction("goal");
    EXPECT_EQ(vm->pendingMinute(), "21");
    EXPECT_EQ(vm->pendingMinuteError(), "");
    vm->setPendingMinute("25");
    EXPECT_FALSE(vm->pendingMinuteError().isEmpty()); // not played yet
    vm->setPendingMinute("abc");
    EXPECT_FALSE(vm->pendingMinuteError().isEmpty());
    vm->setPendingMinute("5");
    EXPECT_EQ(vm->pendingMinuteError(), "");
    ASSERT_TRUE(vm->choosePlayer(player(9)));
    EXPECT_EQ(vm->pendingMinute(), "5"); // kept for the assist step
    ASSERT_TRUE(vm->skipAssist());
    EXPECT_EQ(vm->timeline()->value(0, "minuteText"), "5'");
    EXPECT_EQ(vm->pendingMinute(), "");

    // A red card at 3' for the scorer of 5' is refused, naming that goal.
    Messages errors(vm.get(), &MatchDayViewModel::errorOccurred);
    vm->beginAction("red");
    vm->setPendingMinute("3");
    EXPECT_FALSE(vm->choosePlayer(player(9)));
    ASSERT_EQ(errors.texts().size(), 1);
    EXPECT_EQ(errors.texts().front(),
              "This cannot be recorded at 3': it conflicts with a later event, Goal · Gracz "
              "Numer09 5'. Undo or correct that event first.");
    EXPECT_EQ(vm->timeline()->rowCount(), 1);
    // A minute not played yet.
    vm->setPendingMinute("30");
    EXPECT_FALSE(vm->choosePlayer(player(4)));
    EXPECT_EQ(errors.texts().size(), 2);
}

TEST_F(MatchDayViewModelTest, NotesOnTheTimeline) {
    const auto vm = kickedOff();
    m_clock->advance(15min);
    EXPECT_TRUE(vm->canAddNote());
    EXPECT_EQ(vm->defaultNoteMinute(), "16");
    EXPECT_FALSE(vm->noteMinuteError("20").isEmpty());
    EXPECT_EQ(vm->noteMinuteError("12"), "");
    EXPECT_EQ(vm->notePlayerOptions().size(), 15); // no player + 14
    ASSERT_TRUE(vm->addNote("12", player(7), "Uraz kostki"));
    ASSERT_EQ(vm->timeline()->rowCount(), 1);
    EXPECT_EQ(vm->timeline()->value(0, "kind"), "note");
    EXPECT_EQ(vm->timeline()->value(0, "typeKey"), "note");
    EXPECT_EQ(vm->timeline()->value(0, "typeText"), "Note");
    EXPECT_EQ(vm->timeline()->value(0, "minuteText"), "12'");
    EXPECT_EQ(vm->timeline()->value(0, "playerText"), "Gracz Numer07");
    EXPECT_EQ(vm->timeline()->value(0, "relatedText"), "Uraz kostki");
    EXPECT_EQ(vm->timeline()->value(0, "noteMinute"), "12");
    EXPECT_EQ(vm->undoText(), "Note · Uraz kostki 12'");
    const qint64 noteId = vm->timeline()->value(0, "noteId").toLongLong();

    // A goal at 14' comes above the note on the timeline.
    vm->beginAction("goal");
    vm->setPendingMinute("14");
    ASSERT_TRUE(vm->choosePlayer(player(9)));
    ASSERT_TRUE(vm->skipAssist());
    EXPECT_EQ(vm->timeline()->value(0, "kind"), "event");
    EXPECT_EQ(vm->timeline()->value(1, "kind"), "note");

    ASSERT_TRUE(vm->updateNote(noteId, "11", 0, "Kostka"));
    EXPECT_EQ(vm->timeline()->value(1, "relatedText"), "Kostka");
    EXPECT_EQ(vm->timeline()->value(1, "playerText"), "");
    EXPECT_EQ(vm->deleteNote(noteId), "Note deleted: 11'");
    EXPECT_EQ(vm->timeline()->rowCount(), 1);
    vm->restoreDeletedNote();
    EXPECT_EQ(vm->timeline()->rowCount(), 2);
    EXPECT_EQ(vm->goalsFor(), 1);
}

TEST_F(MatchDayViewModelTest, SevenPlayersWarnAndFewerEndTheMatch) {
    const auto vm = kickedOff();
    m_clock->advance(10min);
    Messages notices(vm.get(), &MatchDayViewModel::notice);
    SignalCount tooFew(vm.get(), &MatchDayViewModel::tooFewPlayersReached);
    for (const int i : {2, 3, 4, 5}) {
        vm->beginAction("red");
        ASSERT_TRUE(vm->choosePlayer(player(i)));
    }
    EXPECT_EQ(vm->onPitchCount(), 7);
    EXPECT_EQ(notices.texts().size(), 1);
    EXPECT_EQ(tooFew.count(), 0);
    vm->beginAction("red");
    ASSERT_TRUE(vm->choosePlayer(player(6)));
    EXPECT_EQ(tooFew.count(), 1);
    EXPECT_TRUE(vm->tooFewPlayers());
    EXPECT_FALSE(vm->canRecordEvents());
    EXPECT_FALSE(vm->canAdvanceClock());
    EXPECT_TRUE(vm->canAddNote());
    EXPECT_TRUE(vm->canUndo());
    vm->beginAction("goal");
    EXPECT_EQ(vm->pendingAction(), "");
    ASSERT_TRUE(vm->endMatch());
    EXPECT_EQ(vm->statusKey(), "finished");
    EXPECT_EQ(vm->undoText(), "early end of the match");
    EXPECT_FALSE(vm->tooFewPlayers());
}

// --- P13 re-review ---

TEST_F(MatchDayViewModelTest, PlayersAreChosenByWhereTheyWereAtTheMinute) {
    // 5 went off for 12 at 21' (stored 20); at 30:00 a goal from 11' (stored 10) is entered.
    const auto vm = kickedOff();
    m_clock->advance(20min + 30s);
    vm->beginAction("sub");
    ASSERT_TRUE(vm->choosePlayer(player(5)));
    ASSERT_TRUE(vm->choosePlayer(player(12)));
    m_clock->advance(9min + 30s);

    vm->beginAction("goal");
    // Now: 5 is off (not selectable), 12 on the pitch.
    EXPECT_FALSE(vm->bench()->value(rowOf(*vm->bench(), player(5)), "selectable").toBool());
    EXPECT_TRUE(vm->pitch()->value(rowOf(*vm->pitch(), player(12)), "selectable").toBool());
    vm->setPendingMinute("11");
    // At 11': 5 on the pitch, 12 still on the bench.
    const int off = rowOf(*vm->bench(), player(5));
    EXPECT_TRUE(vm->bench()->value(off, "selectable").toBool());
    EXPECT_EQ(vm->bench()->value(off, "historyText"), "At 11': on the pitch");
    EXPECT_FALSE(vm->pitch()->value(rowOf(*vm->pitch(), player(12)), "selectable").toBool());
    EXPECT_EQ(vm->pitch()->value(rowOf(*vm->pitch(), player(9)), "historyText"), "");
    ASSERT_TRUE(vm->choosePlayer(player(5)));
    // The assistant too: 12 not yet on the pitch at 11'.
    EXPECT_FALSE(vm->pitch()->value(rowOf(*vm->pitch(), player(12)), "selectable").toBool());
    ASSERT_TRUE(vm->skipAssist());
    EXPECT_EQ(vm->goalsFor(), 1);
    EXPECT_EQ(vm->timeline()->value(1, "minuteText"), "11'");
    EXPECT_EQ(vm->timeline()->value(1, "playerText"), "Gracz Numer05");

    // A substitution entered late: who goes off and who comes on, at 6'.
    vm->beginAction("sub");
    vm->setPendingMinute("6");
    EXPECT_TRUE(vm->bench()->value(rowOf(*vm->bench(), player(5)), "selectable").toBool());
    ASSERT_TRUE(vm->choosePlayer(player(4)));
    // Coming on at 6': 12 was on the bench then (and is on the pitch now).
    const int now = rowOf(*vm->pitch(), player(12));
    EXPECT_TRUE(vm->pitch()->value(now, "selectable").toBool());
    EXPECT_EQ(vm->pitch()->value(now, "historyText"), "At 6': on the bench");
    vm->cancelAction();
    EXPECT_EQ(vm->pitch()->value(now, "historyText"), "");
}

// --- Set clock and the mode without the clock (P13 manual test) ---

TEST_F(MatchDayViewModelTest, SetClockByTheTimeTheClockShows) {
    const auto vm = kickedOff();
    EXPECT_TRUE(vm->canSetClock());
    m_clock->advance(1min);
    EXPECT_FALSE(vm->clockTextError("x").isEmpty());
    EXPECT_FALSE(vm->clockTextError("46:00").isEmpty()); // in stoppage time: 45+1:00
    EXPECT_EQ(vm->clockTextError("4:30"), "");
    EXPECT_EQ(vm->setClock("4:30"), "Clock set to 04:30.");
    EXPECT_EQ(vm->clockText(), "04:30");
    m_clock->advance(10s);
    EXPECT_EQ(vm->clockText(), "04:40");
    vm->undoSetClock();
    EXPECT_EQ(vm->clockText(), "01:10");
    EXPECT_EQ(vm->setClock("45+1:30"), "Clock set to 45+1:30.");
    EXPECT_EQ(vm->clockActionText(), "End 1st half");
}

TEST_F(MatchDayViewModelTest, AMatchBeforeTodayStartsWithoutTheClock) {
    saveLineup();
    OpponentDraft opponent;
    opponent.name = "Wczorajszy";
    MatchDraft past;
    past.opponentId = registry().calendar().addOpponent(opponent).value();
    past.date = Date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{25}};
    const MatchId pastMatch = registry().calendar().addMatch(m_team, past).value();
    Lineup lineup = emptyLineup(*findFormation("4-4-2"));
    for (std::size_t slot = 0; slot < lineup.pitchSlots.size(); ++slot) {
        lineup.pitchSlots[slot].player = m_players[slot];
    }
    lineup.bench = {m_players[11]};
    ASSERT_TRUE(registry().lineup().save(pastMatch, lineup).has_value());

    auto vm = std::make_unique<MatchDayViewModel>();
    vm->setTeamId(m_team.value);
    vm->openMatch(m_match.value);
    EXPECT_FALSE(vm->defaultClockless()); // today
    vm->openMatch(pastMatch.value);
    EXPECT_TRUE(vm->defaultClockless());

    ASSERT_TRUE(vm->start("2x45", 0, 0, true));
    EXPECT_TRUE(vm->clockless());
    EXPECT_EQ(vm->phaseText(), "Without the clock");
    EXPECT_EQ(vm->clockText(), "—");
    EXPECT_FALSE(vm->canAdvanceClock());
    EXPECT_FALSE(vm->canSetClock());
    EXPECT_TRUE(vm->canRecordEvents());
    EXPECT_TRUE(vm->canEndMatch());
    EXPECT_TRUE(vm->canCancelStart());
    EXPECT_EQ(vm->defaultNoteMinute(), "");

    // Every action takes a minute typed, in any order.
    Messages errors(vm.get(), &MatchDayViewModel::errorOccurred);
    vm->beginAction("goal");
    EXPECT_EQ(vm->pendingMinute(), "");
    EXPECT_EQ(vm->pendingMinuteError(), "Enter the minute of the action.");
    ASSERT_TRUE(vm->choosePlayer(player(9)));
    EXPECT_FALSE(vm->skipAssist());
    EXPECT_EQ(errors.texts().size(), 1);
    vm->setPendingMinute("91");
    EXPECT_EQ(vm->pendingMinuteError(), "This minute is outside the match.");
    vm->setPendingMinute("80");
    ASSERT_TRUE(vm->skipAssist());
    vm->beginAction("sub");
    vm->setPendingMinute("46");
    ASSERT_TRUE(vm->choosePlayer(player(5)));
    ASSERT_TRUE(vm->choosePlayer(player(12)));
    vm->beginAction("opponentGoal");
    vm->setPendingMinute("45+2");
    ASSERT_TRUE(vm->confirmAction());
    EXPECT_EQ(vm->timeline()->value(0, "minuteText"), "80'");
    EXPECT_EQ(vm->timeline()->value(1, "minuteText"), "46'");
    EXPECT_EQ(vm->timeline()->value(2, "minuteText"), "45+2'");
    EXPECT_FALSE(vm->canCancelStart());
    EXPECT_EQ(vm->endMinute(), "80");
    ASSERT_TRUE(vm->addNote("12", 0, "Boisko mokre"));

    ASSERT_TRUE(vm->endMatch());
    EXPECT_EQ(vm->statusKey(), "finished");
    EXPECT_EQ(vm->goalsFor(), 1);
    EXPECT_EQ(vm->goalsAgainst(), 1);
    EXPECT_EQ(vm->clockText(), "90:00");
}

// --- Live tactics (packet P13b). The lineup by slot: 0 GK 1, 1 RB 2, 2 CB 3, 3 CB 4, 4 LB 5, 5 RM
// 6, 6 CM 7, 7 CM 8, 8 LM 9, 9 ST 10, 10 ST 11. ---

TEST_F(MatchDayViewModelTest, AChangeOfShapeIsConfirmedWithItsMinuteAndUndone) {
    const auto vm = kickedOff();
    m_clock->advance(30min);
    ASSERT_EQ(vm->board()->rowCount(), 11);
    EXPECT_EQ(vm->shapeText(), "4-4-2");
    EXPECT_EQ(vm->shapeFormation(), "4-4-2");
    EXPECT_TRUE(vm->canChangeTactics());

    ASSERT_TRUE(vm->changeFormation("4-2-3-1"));
    EXPECT_EQ(vm->pendingAction(), "tactic");
    EXPECT_TRUE(vm->tacticsDirty());
    EXPECT_EQ(vm->shapeText(), "4-2-3-1");
    EXPECT_EQ(vm->pendingMinute(), "31");
    EXPECT_EQ(vm->pendingPrompt(),
              "Change of shape: 4-4-2 → 4-2-3-1. Check the minute and confirm.");
    EXPECT_EQ(vm->board()->value(6, "positionKey"), "dm");
    // The tiles still show the shape on the pitch until Confirm.
    EXPECT_EQ(vm->pitch()->value(rowOf(*vm->pitch(), player(7)), "positionCode"), "CM");

    vm->setPendingMinute("30");
    ASSERT_TRUE(vm->confirmAction());
    EXPECT_EQ(vm->pendingAction(), "");
    EXPECT_FALSE(vm->tacticsDirty());
    EXPECT_EQ(vm->shapeText(), "4-2-3-1");
    EXPECT_EQ(vm->pitch()->value(rowOf(*vm->pitch(), player(7)), "positionCode"), "DM");
    EXPECT_EQ(vm->timeline()->value(0, "kind"), "tactic");
    EXPECT_EQ(vm->timeline()->value(0, "iconName"), "tactic");
    EXPECT_EQ(vm->timeline()->value(0, "typeText"), "Change of shape");
    EXPECT_EQ(vm->timeline()->value(0, "playerText"), "4-4-2 → 4-2-3-1");
    EXPECT_EQ(vm->timeline()->value(0, "minuteText"), "30'");
    EXPECT_EQ(vm->undoText(), "Change of shape · 4-4-2 → 4-2-3-1 30'");

    EXPECT_EQ(vm->undo(), "Undone: Change of shape · 4-4-2 → 4-2-3-1 30'");
    EXPECT_EQ(vm->shapeText(), "4-4-2");
    EXPECT_EQ(vm->timeline()->rowCount(), 0);
}

TEST_F(MatchDayViewModelTest, ADraftIsDroppedByCancelSilentlyAndByAnotherActionWithANotice) {
    const auto vm = kickedOff();
    m_clock->advance(10min);
    Messages notices(vm.get(), &MatchDayViewModel::notice);
    ASSERT_TRUE(vm->changeFormation("4-3-3"));
    vm->cancelAction();
    EXPECT_FALSE(vm->tacticsDirty());
    EXPECT_EQ(vm->shapeText(), "4-4-2");
    EXPECT_TRUE(notices.texts().isEmpty());

    ASSERT_TRUE(vm->changeFormation("4-3-3"));
    vm->beginAction("goal");
    EXPECT_FALSE(vm->tacticsDirty());
    EXPECT_EQ(vm->pendingAction(), "scorer");
    EXPECT_EQ(vm->shapeText(), "4-4-2");
    ASSERT_EQ(notices.texts().size(), 1);
    EXPECT_EQ(notices.texts().front(), "The unconfirmed change of shape was discarded.");

    // Undo drops it too.
    vm->cancelAction();
    ASSERT_TRUE(vm->changeFormation("4-3-3"));
    EXPECT_FALSE(vm->undo().isEmpty());
    EXPECT_FALSE(vm->tacticsDirty());
    EXPECT_EQ(notices.texts().size(), 2);
}

TEST_F(MatchDayViewModelTest, TheKeyboardPathMovesAndSwapsPlayers) {
    const auto vm = kickedOff();
    m_clock->advance(10min);
    Messages errors(vm.get(), &MatchDayViewModel::errorOccurred);
    vm->selectSlot(1);
    EXPECT_EQ(vm->selectedSlot(), 1);
    EXPECT_EQ(vm->selectedPosition(), "rb");
    EXPECT_TRUE(vm->board()->value(1, "selected").toBool());

    ASSERT_TRUE(vm->setSlotPosition(1, "rwb"));
    EXPECT_EQ(vm->board()->value(1, "positionKey"), "rwb");
    EXPECT_EQ(vm->shapeText(), "3-1-4-2 (custom)");
    EXPECT_EQ(vm->shapeFormation(), "custom");
    EXPECT_EQ(vm->formationOptions().front().toMap().value("value"), "custom");
    // Back to where he was: nothing left to confirm.
    ASSERT_TRUE(vm->setSlotPosition(1, "rb"));
    EXPECT_FALSE(vm->tacticsDirty());
    EXPECT_EQ(vm->pendingAction(), "");

    // A second goalkeeper is refused.
    EXPECT_FALSE(vm->setSlotPosition(5, "gk"));
    ASSERT_EQ(errors.texts().size(), 1);
    EXPECT_EQ(errors.texts().front(),
              "Only one player can be in goal. Move the goalkeeper out first.");

    // The two strikers swap places: the players stay in their slots.
    const double leftX = vm->board()->value(9, "pitchX").toDouble();
    const double rightX = vm->board()->value(10, "pitchX").toDouble();
    ASSERT_TRUE(vm->swapSlots(9, 10));
    EXPECT_EQ(vm->board()->value(9, "playerId").toLongLong(), player(10));
    EXPECT_DOUBLE_EQ(vm->board()->value(9, "pitchX").toDouble(), rightX);
    EXPECT_DOUBLE_EQ(vm->board()->value(10, "pitchX").toDouble(), leftX);
    EXPECT_EQ(vm->pendingAction(), "tactic");
    EXPECT_EQ(vm->pendingPrompt(),
              "Change of shape: 4-4-2: positions changed. Check the minute and confirm.");
    ASSERT_TRUE(vm->confirmAction());
    EXPECT_EQ(vm->timeline()->value(0, "playerText"), "4-4-2: positions changed");
}

TEST_F(MatchDayViewModelTest, TheBoardWarnsWithoutAGoalkeeper) {
    const auto vm = kickedOff();
    m_clock->advance(10min);
    EXPECT_FALSE(vm->noGoalkeeper());
    vm->beginAction("red");
    ASSERT_TRUE(vm->choosePlayer(player(1)));
    EXPECT_TRUE(vm->noGoalkeeper());
    EXPECT_TRUE(vm->board()->value(0, "hidden").toBool());
    EXPECT_EQ(vm->shapeText(), "4-4-2");
    // The right back goes in goal: the empty slot of the goalkeeper does not block it.
    ASSERT_TRUE(vm->setSlotPosition(1, "gk"));
    EXPECT_FALSE(vm->noGoalkeeper());
    EXPECT_EQ(vm->shapeText(), "3-4-2");
    ASSERT_TRUE(vm->confirmAction());
    EXPECT_FALSE(vm->noGoalkeeper());

    // Moving the goalkeeper out without a new one is allowed, with the warning.
    m_clock->advance(5min);
    ASSERT_TRUE(vm->setSlotPosition(1, "cb"));
    EXPECT_TRUE(vm->noGoalkeeper());
    ASSERT_TRUE(vm->confirmAction());
    EXPECT_TRUE(vm->noGoalkeeper());
}

TEST_F(MatchDayViewModelTest, ABenchPlayerDroppedOnATokenIsASubstitutionWaitingForSave) {
    const auto vm = kickedOff();
    m_clock->advance(20min);
    Messages errors(vm.get(), &MatchDayViewModel::errorOccurred);
    ASSERT_TRUE(vm->substituteOnBoard(9, player(12)));
    EXPECT_EQ(vm->pendingAction(), "subConfirm");
    EXPECT_EQ(vm->pendingPrompt(), "Substitution: Gracz Numer12 comes on for Gracz Numer10. Check "
                                   "the minute and save.");
    EXPECT_EQ(vm->pendingMinute(), "21");
    // Nothing is written before Save.
    EXPECT_EQ(vm->board()->value(9, "playerId").toLongLong(), player(10));
    vm->setPendingMinute("18");
    ASSERT_TRUE(vm->confirmAction());
    EXPECT_EQ(vm->pendingAction(), "");
    EXPECT_EQ(vm->board()->value(9, "playerId").toLongLong(), player(12));
    EXPECT_EQ(vm->board()->value(9, "positionKey"), "st");
    EXPECT_EQ(vm->timeline()->value(0, "minuteText"), "18'");

    // A player on the pitch cannot come on; nor while a change of shape waits.
    EXPECT_FALSE(vm->substituteOnBoard(1, player(3)));
    ASSERT_TRUE(vm->changeFormation("4-3-3"));
    EXPECT_FALSE(vm->substituteOnBoard(1, player(13)));
    ASSERT_EQ(errors.texts().size(), 2);
    EXPECT_EQ(errors.texts().back(),
              "Confirm or cancel the change of shape first, then make the substitution.");
    EXPECT_EQ(vm->pendingAction(), "tactic");
}

TEST_F(MatchDayViewModelTest, AChangeBeforeAStoredOneIsRefusedNamingIt) {
    const auto vm = kickedOff();
    m_clock->advance(30min);
    Messages errors(vm.get(), &MatchDayViewModel::errorOccurred);
    ASSERT_TRUE(vm->changeFormation("4-3-3"));
    ASSERT_TRUE(vm->confirmAction());
    ASSERT_TRUE(vm->changeFormation("4-4-2"));
    vm->setPendingMinute("10");
    EXPECT_FALSE(vm->confirmAction());
    ASSERT_EQ(errors.texts().size(), 1);
    EXPECT_EQ(errors.texts().front(),
              "A later change of shape is already recorded: 4-4-2 → 4-3-3 at 31'. A change can be "
              "entered only at or after that minute.");
    // The draft stays for another minute.
    EXPECT_EQ(vm->pendingAction(), "tactic");
    vm->setPendingMinute("31");
    EXPECT_TRUE(vm->confirmAction());
    EXPECT_EQ(vm->shapeText(), "4-4-2");
}

TEST_F(MatchDayViewModelTest, OtherRolesSeeTheBoardReadOnly) {
    const auto vm = kickedOff();
    m_clock->advance(10min);
    logInAs(Role::Analyst);
    const auto viewer = open();
    EXPECT_EQ(viewer->board()->rowCount(), 11);
    EXPECT_FALSE(viewer->canChangeTactics());
    EXPECT_FALSE(viewer->changeFormation("4-3-3"));
    EXPECT_FALSE(viewer->substituteOnBoard(9, player(12)));
    EXPECT_EQ(viewer->pendingAction(), "");
    EXPECT_EQ(viewer->shapeText(), "4-4-2");
}

TEST_F(MatchDayViewModelTest, TheShapeCannotChangeBeforeTheKickOff) {
    saveLineup();
    const auto vm = open();
    ASSERT_TRUE(vm->start("2x45", 0, 0));
    EXPECT_EQ(vm->board()->rowCount(), 11);
    EXPECT_FALSE(vm->canChangeTactics());
    EXPECT_FALSE(vm->changeFormation("4-3-3"));
    EXPECT_EQ(vm->pendingAction(), "");
}
