#include "viewmodels/MatchReportViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/algorithms/Formations.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/Player.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "services/CalendarService.hpp"
#include "services/LineupService.hpp"
#include "services/MatchDayService.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/PlayerMatchesModel.hpp"

#include <QSqlQuery>
#include <QStringList>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::MatchRatingsModel;
using fdss::viewmodels::MatchReportViewModel;
using fdss::viewmodels::PlayerMatchesModel;

namespace {

// FakeClock: today is Sunday 2026-11-01; the match is today, played in Match Day without the
// clock: a goal of player 10 (assist 9) at 20', an opponent goal at 80'.
constexpr Date kMatchDate{std::chrono::year{2026}, std::chrono::November, std::chrono::day{1}};
constexpr int kSquad = 15;
constexpr int kRating = MatchRatingsModel::RatingColumn;
constexpr int kRpe = MatchRatingsModel::RpeColumn;
// Far enough apart for two digits not to make 10.
constexpr qint64 kApart = 10 * MatchReportViewModel::kTenWindowMs;

class Messages {
public:
    template <class Signal> Messages(const MatchReportViewModel* vm, Signal signal) {
        QObject::connect(vm, signal, &m_context, [this](const QString& text) { m_texts << text; });
    }
    [[nodiscard]] const QStringList& texts() const { return m_texts; }

private:
    QObject m_context;
    QStringList m_texts;
};

class MatchReportViewModelTest : public ViewModelTest {
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
            draft.primaryPosition = i == 1 ? Position::Gk : Position::Cm;
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
        match.competition = "Liga";
        const auto matchId = registry().calendar().addMatch(m_team, match);
        ASSERT_TRUE(matchId.has_value());
        m_match = *matchId;

        Lineup lineup = emptyLineup(*findFormation("4-4-2"));
        for (std::size_t slot = 0; slot < lineup.pitchSlots.size(); ++slot) {
            lineup.pitchSlots[slot].player = m_players[slot];
        }
        lineup.bench = {m_players[11], m_players[12], m_players[13]};
        ASSERT_TRUE(registry().lineup().save(m_match, lineup).has_value());
        auto& matchDay = registry().matchDay();
        ASSERT_TRUE(matchDay.start(m_match, {2, 45}, 4, true).has_value());
        ASSERT_TRUE(
            matchDay.recordGoal(m_match, m_players[9], m_players[8], MatchTime{20}).has_value());
        ASSERT_TRUE(matchDay.recordOpponentGoal(m_match, MatchTime{80}).has_value());
        ASSERT_TRUE(matchDay.endMatch(m_match).has_value());
    }

    std::unique_ptr<MatchReportViewModel> open() {
        auto vm = std::make_unique<MatchReportViewModel>();
        vm->openMatch(m_match.value);
        return vm;
    }

    void logInAs(Role role) {
        const auto id = addStaff("Ktoś Inny", role);
        ASSERT_TRUE(registry().auth().login(id, kPin).has_value());
    }

    [[nodiscard]] qint64 player(int i) const {
        return m_players[static_cast<std::size_t>(i - 1)].value;
    }

    [[nodiscard]] static int rowOf(const MatchRatingsModel& model, qint64 player) {
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.value(row, "playerId").toLongLong() == player) {
                return row;
            }
        }
        return -1;
    }

    [[nodiscard]] Appearance stored(int i) const {
        const auto rows = registry().matchReport().report(m_match);
        EXPECT_TRUE(rows.has_value());
        for (const Appearance& row : rows->appearances) {
            if (row.playerId == m_players[static_cast<std::size_t>(i - 1)]) {
                return row;
            }
        }
        ADD_FAILURE() << "no appearance of player " << i;
        return {};
    }

    // A finished match two days ago that Match Day did not run (an import): players 1–11 from 0
    // to `finalMinute`, 0:0.
    MatchId addOutsideMatch(int finalMinute) {
        MatchDraft draft;
        draft.opponentId = registry().calendar().listOpponents()->front().id;
        draft.date = Date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{30}};
        const auto id = registry().calendar().addMatch(m_team, draft);
        EXPECT_TRUE(id.has_value());
        fdss::data::MatchRepository matches(*m_db);
        std::vector<AppearanceDraft> rows;
        for (int i = 0; i < 11; ++i) {
            AppearanceDraft row;
            row.playerId = m_players[static_cast<std::size_t>(i)];
            row.minuteOn = 0;
            row.minuteOff = finalMinute;
            rows.push_back(row);
        }
        EXPECT_TRUE(matches.replaceAppearances(*id, rows).has_value());
        EXPECT_TRUE(matches.setResult(*id, 0, 0, MatchStatus::Finished).has_value());
        return id.value_or(MatchId{});
    }

    void runSql(const char* statement) const {
        QSqlQuery query(m_db->database());
        ASSERT_TRUE(query.exec(QString::fromUtf8(statement)));
    }

    [[nodiscard]] qint64 timelineEvent(const MatchReportViewModel& vm, const char* typeKey) const {
        for (int row = 0; row < vm.timeline()->rowCount(); ++row) {
            if (vm.timeline()->value(row, "typeKey").toString() == QLatin1StringView(typeKey)) {
                return vm.timeline()->value(row, "eventId").toLongLong();
            }
        }
        return 0;
    }

    TeamId m_team;
    MatchId m_match;
    std::vector<PlayerId> m_players;
};

} // namespace

TEST_F(MatchReportViewModelTest, TheHeaderAndTheGrid) {
    const auto vm = open();
    ASSERT_TRUE(vm->hasReport());
    EXPECT_TRUE(vm->loadError().isEmpty());
    EXPECT_EQ(vm->opponentName(), QStringLiteral("Rywal"));
    EXPECT_EQ(vm->date(), QStringLiteral("2026-11-01"));
    EXPECT_EQ(vm->competition(), QStringLiteral("Liga"));
    EXPECT_EQ(vm->goalsFor(), 1);
    EXPECT_EQ(vm->goalsAgainst(), 1);
    EXPECT_EQ(vm->resultKey(), QStringLiteral("draw"));
    EXPECT_FALSE(vm->readOnly());
    EXPECT_TRUE(vm->correctable());
    EXPECT_FALSE(vm->notFinished());
    EXPECT_FALSE(vm->dirty());
    const MatchRatingsModel& grid = *vm->ratings();
    ASSERT_EQ(grid.rowCount(), 11);
    const int scorer = rowOf(grid, player(10));
    EXPECT_EQ(grid.value(scorer, "goals").toInt(), 1);
    EXPECT_EQ(grid.value(scorer, "minutes").toInt(), 90);
    EXPECT_EQ(grid.value(rowOf(grid, player(9)), "assists").toInt(), 1);
    EXPECT_FALSE(grid.value(scorer, "rating").isValid());
    EXPECT_EQ(vm->timeline()->rowCount(), 2);
    EXPECT_EQ(vm->timeline()->value(0, "typeKey").toString(), QStringLiteral("opponentGoal"));
    ASSERT_EQ(vm->shapes()->rowCount(), 1);
    EXPECT_EQ(vm->shapes()->value(0, "formation").toString(), QStringLiteral("4-4-2"));
    EXPECT_EQ(vm->shapes()->value(0, "minutes").toInt(), 90);
}

TEST_F(MatchReportViewModelTest, TheKeyboardPathRatesAndSaves) {
    const auto vm = open();
    SignalCount dirty(vm.get(), &MatchReportViewModel::dirtyChanged);
    const MatchRatingsModel& grid = *vm->ratings();
    // Row 0: 1 then 0 quickly is 10; RPE 7.
    vm->rememberCell(0, kRating);
    vm->enterDigitAt(0, kRating, 1, 1000);
    vm->enterDigitAt(0, kRating, 0, 1000 + MatchReportViewModel::kTenWindowMs);
    EXPECT_EQ(grid.value(0, "rating").toInt(), 10);
    EXPECT_TRUE(vm->dirty());
    EXPECT_TRUE(grid.value(0, "changed").toBool());
    vm->rememberCell(0, kRpe);
    vm->enterDigitAt(0, kRpe, 7, 5000);
    // Row 1: 1 then 0 slowly is 0, which is no rating.
    vm->enterDigitAt(1, kRating, 1, 10000);
    vm->enterDigitAt(1, kRating, 0, 10000 + kApart);
    EXPECT_EQ(grid.value(1, "rating").toInt(), 0);
    EXPECT_FALSE(grid.value(1, "rowError").toString().isEmpty());
    EXPECT_EQ(vm->firstErrorRow(), 1);
    Messages errors(vm.get(), &MatchReportViewModel::errorOccurred);
    EXPECT_FALSE(vm->save());
    EXPECT_EQ(errors.texts().size(), 1);
    // Esc goes back to what the cell held when it got the focus; Delete clears.
    vm->rememberCell(1, kRating);
    vm->enterDigitAt(1, kRating, 6, 20000);
    vm->revertCell(1, kRating);
    EXPECT_EQ(grid.value(1, "rating").toInt(), 0);
    vm->clearCell(1, kRating);
    EXPECT_FALSE(grid.value(1, "rating").isValid());
    EXPECT_EQ(vm->firstErrorRow(), -1);
    // 1 and 0 in two different cells is not 10.
    vm->enterDigitAt(2, kRating, 1, 30000);
    vm->enterDigitAt(2, kRpe, 0, 30000 + 1);
    EXPECT_EQ(grid.value(2, "rating").toInt(), 1);
    EXPECT_EQ(grid.value(2, "rpe").toInt(), 0);

    vm->setNote(QStringLiteral("Dobry mecz"));
    SignalCount saved(vm.get(), &MatchReportViewModel::saved);
    ASSERT_TRUE(vm->save());
    EXPECT_EQ(saved.count(), 1);
    EXPECT_FALSE(vm->dirty());
    EXPECT_GT(dirty.count(), 0);
    const qint64 first = grid.value(0, "playerId").toLongLong();
    const auto report = registry().matchReport().report(m_match).value();
    const auto row =
        std::ranges::find(report.appearances, PlayerId{first}, &AppearanceDraft::playerId);
    EXPECT_EQ(row->rating, 10);
    EXPECT_EQ(row->rpe, 7);
    EXPECT_EQ(report.reportNote, std::string("Dobry mecz"));
    EXPECT_EQ(vm->note(), QStringLiteral("Dobry mecz"));
}

TEST_F(MatchReportViewModelTest, DirtyTracksTheNoteAndGoesBackWithTheValue) {
    const auto vm = open();
    vm->setNote(QStringLiteral("x"));
    EXPECT_TRUE(vm->dirty());
    vm->setNote(QString{});
    EXPECT_FALSE(vm->dirty());
    vm->enterDigitAt(0, kRating, 7, 0);
    EXPECT_TRUE(vm->dirty());
    vm->clearCell(0, kRating);
    EXPECT_FALSE(vm->dirty());
    vm->enterDigitAt(0, kRating, 7, 0);
    vm->discardChanges();
    EXPECT_FALSE(vm->dirty());
    EXPECT_FALSE(vm->ratings()->value(0, "rating").isValid());
}

TEST_F(MatchReportViewModelTest, ACorrectionKeepsTheUnsavedRatings) {
    const auto vm = open();
    vm->enterDigitAt(rowOf(*vm->ratings(), player(4)), kRating, 8, 0);
    ASSERT_TRUE(vm->eventError(QStringLiteral("sub"), QStringLiteral("61"), player(12), player(10))
                    .isEmpty());
    ASSERT_TRUE(vm->addEvent(QStringLiteral("sub"), QStringLiteral("61"), player(12), player(10)));
    const MatchRatingsModel& grid = *vm->ratings();
    EXPECT_EQ(grid.rowCount(), 12);
    EXPECT_EQ(grid.value(rowOf(grid, player(10)), "minutes").toInt(), 60);
    EXPECT_EQ(grid.value(rowOf(grid, player(12)), "minutes").toInt(), 30);
    EXPECT_EQ(grid.value(rowOf(grid, player(4)), "rating").toInt(), 8);
    EXPECT_TRUE(vm->dirty());
    EXPECT_EQ(vm->timeline()->rowCount(), 3);
}

TEST_F(MatchReportViewModelTest, ThePlayersToChooseFollowTheMinute) {
    const auto vm = open();
    ASSERT_TRUE(vm->addEvent(QStringLiteral("sub"), QStringLiteral("61"), player(12), player(10)));
    const auto ids = [](const QVariantList& options) {
        std::vector<qint64> result;
        for (const QVariant& option : options) {
            result.push_back(option.toMap().value(QStringLiteral("value")).toLongLong());
        }
        return result;
    };
    const auto before = ids(
        vm->playerOptions(QStringLiteral("goal"), QStringLiteral("30"), QStringLiteral("first")));
    EXPECT_NE(std::ranges::find(before, player(10)), before.end());
    EXPECT_EQ(std::ranges::find(before, player(12)), before.end());
    const auto after = ids(
        vm->playerOptions(QStringLiteral("goal"), QStringLiteral("70"), QStringLiteral("first")));
    EXPECT_EQ(std::ranges::find(after, player(10)), after.end());
    EXPECT_NE(std::ranges::find(after, player(12)), after.end());
    const auto comingOn = ids(
        vm->playerOptions(QStringLiteral("sub"), QStringLiteral("70"), QStringLiteral("first")));
    EXPECT_EQ(comingOn.size(), 2U); // 13 and 14
    const auto assists = ids(
        vm->playerOptions(QStringLiteral("goal"), QStringLiteral("70"), QStringLiteral("second")));
    EXPECT_EQ(assists.front(), 0); // no assist
    EXPECT_TRUE(
        vm->playerOptions(QStringLiteral("goal"), QStringLiteral("abc"), QStringLiteral("first"))
            .isEmpty());
    EXPECT_FALSE(vm->minuteError(QStringLiteral("91")).isEmpty());
    EXPECT_FALSE(vm->minuteError(QStringLiteral("abc")).isEmpty());
    EXPECT_TRUE(vm->minuteError(QStringLiteral("90+2")).isEmpty());
}

TEST_F(MatchReportViewModelTest, ADependentEventIsNamedAndNothingIsDeleted) {
    const auto vm = open();
    ASSERT_TRUE(vm->addEvent(QStringLiteral("sub"), QStringLiteral("61"), player(12), player(10)));
    ASSERT_TRUE(vm->addEvent(QStringLiteral("goal"), QStringLiteral("71"), player(12), 0));
    qint64 sub = 0;
    for (int row = 0; row < vm->timeline()->rowCount(); ++row) {
        if (vm->timeline()->value(row, "typeKey").toString() == QStringLiteral("sub")) {
            sub = vm->timeline()->value(row, "eventId").toLongLong();
        }
    }
    ASSERT_GT(sub, 0);
    Messages errors(vm.get(), &MatchReportViewModel::errorOccurred);
    EXPECT_FALSE(vm->deleteEvent(sub));
    ASSERT_EQ(errors.texts().size(), 1);
    EXPECT_TRUE(errors.texts().front().contains(QStringLiteral("71'")));
    EXPECT_EQ(vm->timeline()->rowCount(), 4);
    EXPECT_EQ(vm->goalsFor(), 2);
}

TEST_F(MatchReportViewModelTest, DeletingAGoalAsksAndItsUndoBringsItBack) {
    const auto vm = open();
    const qint64 goal = vm->timeline()->value(1, "eventId").toLongLong();
    EXPECT_TRUE(vm->eventRemovesGoal(goal));
    Messages deleted(vm.get(), &MatchReportViewModel::deleted);
    ASSERT_TRUE(vm->deleteEvent(goal));
    EXPECT_EQ(deleted.texts().size(), 1);
    EXPECT_EQ(vm->goalsFor(), 0);
    EXPECT_EQ(vm->resultKey(), QStringLiteral("loss"));
    vm->undoDelete();
    EXPECT_EQ(vm->goalsFor(), 1);
    EXPECT_EQ(vm->timeline()->value(1, "eventId").toLongLong(), goal);
}

TEST_F(MatchReportViewModelTest, NotesOfTheReport) {
    const auto vm = open();
    ASSERT_TRUE(vm->addNote(QStringLiteral("45+2"), player(3), QStringLiteral("Uraz kostki")));
    ASSERT_EQ(vm->timeline()->rowCount(), 3);
    const qint64 note = vm->timeline()->value(1, "noteId").toLongLong();
    ASSERT_GT(note, 0);
    EXPECT_EQ(vm->timeline()->value(1, "noteMinute").toString(), QStringLiteral("45+2"));
    ASSERT_TRUE(vm->updateNote(note, QStringLiteral("50"), 0, QStringLiteral("Wiatr")));
    ASSERT_TRUE(vm->deleteNote(vm->timeline()->value(1, "noteId").toLongLong()));
    EXPECT_EQ(vm->timeline()->rowCount(), 2);
    vm->undoDelete();
    EXPECT_EQ(vm->timeline()->rowCount(), 3);
    EXPECT_GT(vm->notePlayerOptions().size(), 11);
}

TEST_F(MatchReportViewModelTest, OtherRolesReadOnly) {
    logInAs(Role::Physio);
    const auto vm = open();
    EXPECT_TRUE(vm->hasReport());
    EXPECT_TRUE(vm->readOnly());
    EXPECT_FALSE(vm->correctable());
    vm->enterDigitAt(0, kRating, 7, 0);
    EXPECT_FALSE(vm->ratings()->value(0, "rating").isValid());
    vm->setNote(QStringLiteral("x"));
    EXPECT_FALSE(vm->dirty());
    Messages errors(vm.get(), &MatchReportViewModel::errorOccurred);
    EXPECT_FALSE(vm->save());
    EXPECT_FALSE(vm->addEvent(QStringLiteral("yellow"), QStringLiteral("30"), player(4), 0));
    EXPECT_EQ(errors.texts().size(), 2);
}

TEST_F(MatchReportViewModelTest, AMatchLiveAgainIsNotFinished) {
    const auto vm = open();
    ASSERT_TRUE(registry().matchDay().undoLast(m_match).has_value()); // the end
    vm->reload();
    EXPECT_TRUE(vm->notFinished());
    EXPECT_TRUE(vm->readOnly());
    EXPECT_TRUE(vm->resultKey().isEmpty());
}

TEST_F(MatchReportViewModelTest, AMissingMatchIsALoadError) {
    auto vm = std::make_unique<MatchReportViewModel>();
    vm->openMatch(999);
    EXPECT_FALSE(vm->hasReport());
    EXPECT_FALSE(vm->loadError().isEmpty());
    vm->openMatch(0);
    EXPECT_TRUE(vm->loadError().isEmpty());
}

TEST_F(MatchReportViewModelTest, PlayerMatchesShowTheRatingNewestFirst) {
    const auto vm = open();
    const int row = rowOf(*vm->ratings(), player(10));
    vm->enterDigitAt(row, kRating, 8, 0);
    vm->enterDigitAt(row, kRpe, 6, kApart);
    ASSERT_TRUE(vm->save());
    PlayerMatchesModel matches;
    matches.setPlayerId(player(10));
    ASSERT_EQ(matches.count(), 1);
    EXPECT_TRUE(matches.loadError().isEmpty());
    EXPECT_EQ(matches.value(0, "matchId").toLongLong(), m_match.value);
    EXPECT_EQ(matches.value(0, "date").toString(), QStringLiteral("2026-11-01"));
    EXPECT_EQ(matches.value(0, "scoreText").toString(), QStringLiteral("1:1"));
    EXPECT_EQ(matches.value(0, "resultKey").toString(), QStringLiteral("draw"));
    EXPECT_EQ(matches.value(0, "minutes").toInt(), 90);
    EXPECT_EQ(matches.value(0, "goals").toInt(), 1);
    EXPECT_EQ(matches.value(0, "rating").toInt(), 8);
    EXPECT_EQ(matches.value(0, "rpe").toInt(), 6);
    matches.setPlayerId(player(15));
    EXPECT_EQ(matches.count(), 0);
    EXPECT_EQ(stored(10).rating, 8);
}

// --- P14 review ---

TEST_F(MatchReportViewModelTest, AnUnsavedRatingOfASubstituteSurvivesDeleteAndUndo) {
    const auto vm = open();
    ASSERT_TRUE(vm->addEvent(QStringLiteral("sub"), QStringLiteral("61"), player(12), player(10)));
    vm->enterDigitAt(rowOf(*vm->ratings(), player(12)), kRating, 9, 0);
    vm->enterDigitAt(rowOf(*vm->ratings(), player(12)), kRpe, 4, kApart);
    ASSERT_TRUE(vm->dirty());
    ASSERT_TRUE(vm->deleteEvent(timelineEvent(*vm, "sub")));
    EXPECT_EQ(rowOf(*vm->ratings(), player(12)), -1);
    EXPECT_TRUE(vm->dirty()); // the typed rating is still there, only not shown
    vm->undoDelete();
    const int row = rowOf(*vm->ratings(), player(12));
    ASSERT_GE(row, 0);
    EXPECT_EQ(vm->ratings()->value(row, "rating").toInt(), 9);
    EXPECT_EQ(vm->ratings()->value(row, "rpe").toInt(), 4);
    EXPECT_TRUE(vm->dirty());
    ASSERT_TRUE(vm->save());
    EXPECT_EQ(stored(12).rating, 9);
    EXPECT_FALSE(vm->dirty());
}

TEST_F(MatchReportViewModelTest, ASavedRatingOfASubstituteComesBackWithUndo) {
    const auto vm = open();
    ASSERT_TRUE(vm->addEvent(QStringLiteral("sub"), QStringLiteral("61"), player(12), player(10)));
    vm->enterDigitAt(rowOf(*vm->ratings(), player(12)), kRating, 6, 0);
    ASSERT_TRUE(vm->save());
    ASSERT_TRUE(vm->deleteEvent(timelineEvent(*vm, "sub")));
    EXPECT_FALSE(vm->dirty());
    vm->undoDelete();
    const int row = rowOf(*vm->ratings(), player(12));
    ASSERT_GE(row, 0);
    EXPECT_EQ(vm->ratings()->value(row, "rating").toInt(), 6);
    EXPECT_FALSE(vm->dirty());
    EXPECT_EQ(stored(12).rating, 6);
}

TEST_F(MatchReportViewModelTest, UndoStillBelongsToItsMatchAfterAnotherReportOpened) {
    const MatchId other = addOutsideMatch(90);
    const auto vm = open();
    const qint64 goal = timelineEvent(*vm, "goal");
    ASSERT_TRUE(vm->deleteEvent(goal));
    vm->openMatch(other.value);
    ASSERT_EQ(vm->matchId(), other.value);
    Messages errors(vm.get(), &MatchReportViewModel::errorOccurred);
    vm->undoDelete();
    EXPECT_TRUE(errors.texts().isEmpty());
    EXPECT_EQ(vm->matchId(), other.value); // the report shown stays
    const auto report = registry().matchReport().report(m_match);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->match.match.goalsFor, 1);
    EXPECT_EQ(report->events.size(), 2U);
}

TEST_F(MatchReportViewModelTest, TryAgainReadsTheSameMatchAndKeepsTheUnsavedRatings) {
    const auto vm = open();
    vm->enterDigitAt(0, kRating, 8, 0);
    runSql("ALTER TABLE match_note RENAME TO match_note_away");
    vm->reload();
    EXPECT_FALSE(vm->loadError().isEmpty());
    EXPECT_EQ(vm->matchId(), m_match.value);
    runSql("ALTER TABLE match_note_away RENAME TO match_note");
    vm->reload();
    EXPECT_TRUE(vm->loadError().isEmpty());
    ASSERT_TRUE(vm->hasReport());
    EXPECT_EQ(vm->ratings()->value(0, "rating").toInt(), 8);
    EXPECT_TRUE(vm->dirty());

    // A match that cannot be read at all is read again by its id, too.
    runSql("ALTER TABLE match_note RENAME TO match_note_away");
    auto fresh = std::make_unique<MatchReportViewModel>();
    fresh->openMatch(m_match.value);
    EXPECT_FALSE(fresh->hasReport());
    EXPECT_EQ(fresh->matchId(), m_match.value);
    runSql("ALTER TABLE match_note_away RENAME TO match_note");
    fresh->reload();
    EXPECT_TRUE(fresh->hasReport());
}

TEST_F(MatchReportViewModelTest, AMatchWithoutMatchDayTakesTheMinutesOfItsData) {
    const MatchId outside = addOutsideMatch(115);
    auto vm = std::make_unique<MatchReportViewModel>();
    vm->openMatch(outside.value);
    ASSERT_TRUE(vm->hasReport());
    EXPECT_TRUE(vm->correctable());
    EXPECT_TRUE(vm->formatText().isEmpty());
    EXPECT_TRUE(vm->minuteError(QStringLiteral("101")).isEmpty());
    EXPECT_TRUE(vm->minuteError(QStringLiteral("115")).isEmpty());
    EXPECT_FALSE(vm->minuteError(QStringLiteral("117")).isEmpty());
    EXPECT_FALSE(vm->minuteError(QStringLiteral("45+2")).isEmpty());
    ASSERT_TRUE(vm->addEvent(QStringLiteral("goal"), QStringLiteral("101"), player(10), 0));
    EXPECT_EQ(vm->goalsFor(), 1);
}
