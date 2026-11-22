#include "viewmodels/MatchEditorViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Match.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "services/CalendarService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/OpponentListModel.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::ErrorPresenter;
using fdss::viewmodels::MatchEditorViewModel;
using fdss::viewmodels::OpponentListModel;

namespace {

constexpr auto kDay = "2026-11-07";

class MatchEditorViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
    }

    OpponentId addOpponent(const char* name) {
        OpponentDraft draft;
        draft.name = name;
        const auto id = registry().calendar().addOpponent(draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(OpponentId{});
    }

    MatchId addMatch(OpponentId opponent, const char* competition = "Liga") {
        MatchDraft draft;
        draft.opponentId = opponent;
        draft.date = Date{std::chrono::year{2026}, std::chrono::November, std::chrono::day{7}};
        draft.kickoffTime = std::chrono::hours{18};
        draft.venue = Venue::Away;
        draft.competition = competition;
        const auto id = registry().calendar().addMatch(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(MatchId{});
    }

    void logInAs(const char* name, Role role) {
        const StaffUserId id = addStaff(name, role);
        const auto login = registry().auth().login(id, kPin);
        ASSERT_TRUE(login.has_value());
    }

    static std::vector<std::string> opponentNames(const MatchEditorViewModel& editor) {
        std::vector<std::string> names;
        const OpponentListModel* model = editor.opponents();
        for (int row = 0; row < model->rowCount(); ++row) {
            names.push_back(
                model->index(row).data(OpponentListModel::TextRole).toString().toStdString());
        }
        return names;
    }

    TeamId m_team;
};

TEST_F(MatchEditorViewModelTest, AddsAMatchWithEveryField) {
    const OpponentId opponent = addOpponent("Rywal");
    MatchEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding(QString::fromUtf8(kDay));
    EXPECT_TRUE(editor.adding());
    EXPECT_FALSE(editor.readOnly());
    EXPECT_EQ(editor.date(), QString::fromUtf8(kDay));
    editor.setOpponentId(opponent.value);
    editor.setKickoffTime(QStringLiteral("18:30"));
    editor.setVenue(QStringLiteral("neutral"));
    editor.setCompetition(QStringLiteral("Puchar Polski"));
    SignalCount saved(&editor, &MatchEditorViewModel::saved);

    ASSERT_TRUE(editor.save());
    EXPECT_EQ(saved.count(), 1);

    const auto items = registry().calendar().listItems(m_team);
    ASSERT_TRUE(items.has_value());
    ASSERT_EQ(items->matches.size(), 1U);
    const Match& match = items->matches.front().match;
    EXPECT_EQ(match.opponentId, opponent);
    EXPECT_EQ(match.date,
              (Date{std::chrono::year{2026}, std::chrono::November, std::chrono::day{7}}));
    EXPECT_EQ(match.kickoffTime, std::chrono::hours{18} + std::chrono::minutes{30});
    EXPECT_EQ(match.venue, Venue::Neutral);
    EXPECT_EQ(match.competition, "Puchar Polski");
    EXPECT_EQ(match.status, MatchStatus::Planned);
}

TEST_F(MatchEditorViewModelTest, KickoffAndCompetitionAreOptional) {
    const OpponentId opponent = addOpponent("Rywal");
    MatchEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding(QString::fromUtf8(kDay));
    editor.setOpponentId(opponent.value);
    editor.setCompetition(QStringLiteral("   "));

    ASSERT_TRUE(editor.save());

    const auto items = registry().calendar().listItems(m_team);
    ASSERT_TRUE(items.has_value());
    ASSERT_EQ(items->matches.size(), 1U);
    EXPECT_FALSE(items->matches.front().match.kickoffTime.has_value());
    EXPECT_FALSE(items->matches.front().match.competition.has_value());
}

TEST_F(MatchEditorViewModelTest, AnOpponentAndADateAreNeeded) {
    MatchEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding(QString{});

    EXPECT_FALSE(editor.save());

    EXPECT_EQ(editor.opponentError(),
              ErrorPresenter::message(Error{ErrorCode::InvalidInput, {}, "opponent_id"}));
    EXPECT_EQ(editor.dateError(),
              ErrorPresenter::message(Error{ErrorCode::InvalidInput, {}, "date"}));
    editor.setOpponentId(addOpponent("Rywal").value);
    editor.setDate(QString::fromUtf8(kDay));
    EXPECT_TRUE(editor.opponentError().isEmpty());
    EXPECT_TRUE(editor.dateError().isEmpty());
}

TEST_F(MatchEditorViewModelTest, AnImpossibleTimeIsAFieldError) {
    MatchEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding(QString::fromUtf8(kDay));
    editor.setOpponentId(addOpponent("Rywal").value);
    for (const char* time : {"24:00", "18:60", "18", "abc"}) {
        SCOPED_TRACE(time);
        editor.setKickoffTime(QString::fromUtf8(time));
        EXPECT_FALSE(editor.save());
        EXPECT_FALSE(editor.kickoffTimeError().isEmpty());
    }
    editor.setKickoffTime(QStringLiteral("23:59"));
    EXPECT_TRUE(editor.kickoffTimeError().isEmpty());
    EXPECT_TRUE(editor.save());
}

TEST_F(MatchEditorViewModelTest, EditingLoadsAndSavesTheMatch) {
    const OpponentId opponent = addOpponent("Rywal");
    const OpponentId other = addOpponent("Orzeł");
    const MatchId match = addMatch(opponent);
    MatchEditorViewModel editor;
    editor.setTeamId(m_team.value);

    ASSERT_TRUE(editor.startEditing(match.value));
    EXPECT_FALSE(editor.adding());
    EXPECT_EQ(editor.matchId(), match.value);
    EXPECT_EQ(editor.opponentId(), opponent.value);
    EXPECT_EQ(editor.opponentName(), QStringLiteral("Rywal"));
    EXPECT_EQ(editor.date(), QString::fromUtf8(kDay));
    EXPECT_EQ(editor.kickoffTime(), QStringLiteral("18:00"));
    EXPECT_EQ(editor.venue(), QStringLiteral("away"));
    EXPECT_EQ(editor.competition(), QStringLiteral("Liga"));

    editor.setOpponentId(other.value);
    editor.setKickoffTime(QString{});
    ASSERT_TRUE(editor.save());

    const auto stored = registry().calendar().match(match);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->opponentId, other);
    EXPECT_FALSE(stored->kickoffTime.has_value());
}

TEST_F(MatchEditorViewModelTest, AMissingMatchIsReported) {
    MatchEditorViewModel editor;
    SignalCount errors(&editor, &MatchEditorViewModel::errorOccurred);
    EXPECT_FALSE(editor.startEditing(999));
    EXPECT_EQ(errors.count(), 1);
}

TEST_F(MatchEditorViewModelTest, OpponentsAreInPolishAlphabeticalOrder) {
    for (const char* name : {"Zawisza", "Łada", "Lech", "Ślęza", "Sokół", "Mazur", "Żuraw"}) {
        addOpponent(name);
    }
    MatchEditorViewModel editor;
    editor.startAdding(QString::fromUtf8(kDay));

    const std::vector<std::string> expected{"Lech",  "Łada",    "Mazur", "Sokół",
                                            "Ślęza", "Zawisza", "Żuraw"};
    EXPECT_EQ(opponentNames(editor), expected);
}

TEST_F(MatchEditorViewModelTest, ANewOpponentIsSavedAndChosen) {
    addOpponent("Rywal");
    MatchEditorViewModel editor;
    editor.startAdding(QString::fromUtf8(kDay));

    ASSERT_TRUE(editor.addOpponent(QStringLiteral("Łada Biłgoraj")));

    EXPECT_NE(editor.opponentId(), 0);
    EXPECT_EQ(editor.opponentName(), QStringLiteral("Łada Biłgoraj"));
    const std::vector<std::string> expected{"Łada Biłgoraj", "Rywal"};
    EXPECT_EQ(opponentNames(editor), expected);
}

TEST_F(MatchEditorViewModelTest, ANewOpponentClearsTheMissingOpponentError) {
    MatchEditorViewModel editor;
    editor.startAdding(QString::fromUtf8(kDay));
    ASSERT_FALSE(editor.save());
    ASSERT_FALSE(editor.opponentError().isEmpty());

    ASSERT_TRUE(editor.addOpponent(QStringLiteral("Rywal")));
    EXPECT_TRUE(editor.opponentError().isEmpty());
}

TEST_F(MatchEditorViewModelTest, ADuplicateOpponentIsAnErrorOnTheNewField) {
    addOpponent("Łódź");
    MatchEditorViewModel editor;
    editor.startAdding(QString::fromUtf8(kDay));
    SignalCount errors(&editor, &MatchEditorViewModel::errorOccurred);

    EXPECT_FALSE(editor.addOpponent(QStringLiteral("ŁÓDŹ")));

    EXPECT_EQ(editor.newOpponentError(),
              ErrorPresenter::message(Error{ErrorCode::Conflict, {}, "opponent_name"}));
    EXPECT_EQ(errors.count(), 0);
    EXPECT_EQ(editor.opponentId(), 0);
    editor.clearNewOpponentError();
    EXPECT_TRUE(editor.newOpponentError().isEmpty());
}

TEST_F(MatchEditorViewModelTest, AnEmptyOpponentNameIsAnErrorOnTheNewField) {
    MatchEditorViewModel editor;
    editor.startAdding(QString::fromUtf8(kDay));
    EXPECT_FALSE(editor.addOpponent(QStringLiteral("   ")));
    EXPECT_FALSE(editor.newOpponentError().isEmpty());
}

TEST_F(MatchEditorViewModelTest, SuggestionsAreTheCompetitionsOfTheTeam) {
    const OpponentId opponent = addOpponent("Rywal");
    addMatch(opponent, "Liga");
    addMatch(opponent, "Liga");
    MatchEditorViewModel editor;
    editor.setTeamId(m_team.value);
    SignalCount changed(&editor, &MatchEditorViewModel::suggestionsChanged);

    editor.startAdding(QString::fromUtf8(kDay));

    EXPECT_EQ(editor.competitionSuggestions(), QStringList{QStringLiteral("Liga")});
    EXPECT_GE(changed.count(), 1);
}

TEST_F(MatchEditorViewModelTest, AMatchThatIsNotPlannedIsReadOnly) {
    const MatchId match = addMatch(addOpponent("Rywal"));
    ASSERT_TRUE(fdss::data::MatchRepository(*m_db)
                    .setResult(match, 2, 1, MatchStatus::Finished)
                    .has_value());
    MatchEditorViewModel editor;

    ASSERT_TRUE(editor.startEditing(match.value));

    EXPECT_TRUE(editor.readOnly());
    EXPECT_EQ(editor.statusText(), QStringLiteral("Finished"));
    SignalCount errors(&editor, &MatchEditorViewModel::errorOccurred);
    EXPECT_FALSE(editor.save());
    EXPECT_EQ(errors.count(), 1);
}

TEST_F(MatchEditorViewModelTest, TheOpponentOfAPlayedMatchCanBeRenamed) {
    const OpponentId opponent = addOpponent("Wisla");
    addOpponent("Łódź");
    const MatchId match = addMatch(opponent);
    ASSERT_TRUE(fdss::data::MatchRepository(*m_db)
                    .setResult(match, 2, 1, MatchStatus::Finished)
                    .has_value());
    MatchEditorViewModel editor;
    ASSERT_TRUE(editor.startEditing(match.value));
    EXPECT_TRUE(editor.readOnly());
    EXPECT_TRUE(editor.canRenameOpponent());
    SignalCount renamed(&editor, &MatchEditorViewModel::opponentRenamed);
    SignalCount errors(&editor, &MatchEditorViewModel::errorOccurred);

    EXPECT_FALSE(editor.renameOpponent(QStringLiteral("łódź")));
    EXPECT_EQ(editor.renameOpponentError(),
              ErrorPresenter::message(Error{ErrorCode::Conflict, {}, "opponent_name"}));
    EXPECT_TRUE(editor.newOpponentError().isEmpty());
    EXPECT_EQ(errors.count(), 0);

    ASSERT_TRUE(editor.renameOpponent(QStringLiteral("Wisła")));
    EXPECT_TRUE(editor.renameOpponentError().isEmpty());
    EXPECT_EQ(editor.opponentName(), QStringLiteral("Wisła"));
    EXPECT_EQ(editor.opponentId(), opponent.value);
    EXPECT_EQ(renamed.count(), 1);
}

TEST_F(MatchEditorViewModelTest, ARoleWithoutEditCalendarSeesAndCannotChange) {
    const OpponentId opponent = addOpponent("Rywal");
    const MatchId match = addMatch(opponent);
    logInAs("Analityk", Role::Analyst);
    MatchEditorViewModel editor;
    SignalCount errors(&editor, &MatchEditorViewModel::errorOccurred);

    ASSERT_TRUE(editor.startEditing(match.value));
    EXPECT_TRUE(editor.readOnly());
    EXPECT_EQ(editor.opponentId(), opponent.value);

    EXPECT_FALSE(editor.save());
    EXPECT_FALSE(editor.addOpponent(QStringLiteral("Nowy")));
    EXPECT_FALSE(editor.canRenameOpponent());
    EXPECT_FALSE(editor.renameOpponent(QStringLiteral("Nowy")));
    EXPECT_EQ(errors.count(), 3);
    EXPECT_TRUE(registry().calendar().listOpponents()->size() == 1);
}

TEST_F(MatchEditorViewModelTest, AnUnknownVenueIsReportedAndIgnored) {
    MatchEditorViewModel editor;
    editor.startAdding(QString::fromUtf8(kDay));
    SignalCount errors(&editor, &MatchEditorViewModel::errorOccurred);

    editor.setVenue(QStringLiteral("moon"));

    EXPECT_EQ(errors.count(), 1);
    EXPECT_EQ(editor.venue(), QStringLiteral("home"));
}

} // namespace

TEST_F(MatchEditorViewModelTest, AMatchBeforeTodayIsFilledInAfterItWasPlayed) {
    // FakeClock: today is 2026-11-01.
    MatchEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding(QStringLiteral("2026-10-31"));
    EXPECT_TRUE(editor.beforeToday());
    editor.setDate(QStringLiteral("2026-11-01"));
    EXPECT_FALSE(editor.beforeToday());
    editor.setDate(QStringLiteral("not a date"));
    EXPECT_FALSE(editor.beforeToday());
}
