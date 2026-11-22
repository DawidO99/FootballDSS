#include "viewmodels/PreMatchViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/Match.hpp"
#include "core/domain/Opponent.hpp"
#include "core/domain/Player.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "services/CalendarService.hpp"
#include "services/LoadService.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/AppSettings.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <format>
#include <functional>
#include <memory>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::PreMatchViewModel;

namespace {

// FakeClock: today is Sunday 2026-11-01; the match is on Saturday 2026-11-07.
constexpr Date kMatchDate{std::chrono::year{2026}, std::chrono::November, std::chrono::day{7}};

// Players 1..11 fit 4-4-2 (gk rb cb cb lb rm cm cm lm st st), 12..20 play centre midfield.
constexpr std::array<Position, 11> kPositions{
    Position::Gk, Position::Rb, Position::Cb, Position::Cb, Position::Lb, Position::Rm,
    Position::Cm, Position::Cm, Position::Lm, Position::St, Position::St};
constexpr int kSquad = 20;

bool waitUntil(const std::function<bool()>& done) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return done();
}

// Collects the texts of a signal with one QString argument.
class Messages {
public:
    template <class Signal> Messages(const PreMatchViewModel* vm, Signal signal) {
        QObject::connect(vm, signal, &m_context, [this](const QString& text) { m_texts << text; });
    }
    [[nodiscard]] const QStringList& texts() const { return m_texts; }

private:
    QObject m_context;
    QStringList m_texts;
};

class PreMatchViewModelTest : public ViewModelTest {
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
        match.competition = "Liga";
        const auto matchId = registry().calendar().addMatch(m_team, match);
        ASSERT_TRUE(matchId.has_value());
        m_match = *matchId;
    }

    // A view model on the team, with the load statuses in.
    std::unique_ptr<PreMatchViewModel> open() {
        auto vm = std::make_unique<PreMatchViewModel>();
        vm->setTeamId(m_team.value);
        EXPECT_TRUE(waitUntil([&] { return !vm->loadPending(); }));
        return vm;
    }

    // Player i of the squad (1-based).
    [[nodiscard]] qint64 player(int i) const {
        return m_players[static_cast<std::size_t>(i - 1)].value;
    }

    // The keyboard path: select the first slot, then pick players 1..11.
    static void pickEleven(PreMatchViewModel& vm, const std::vector<qint64>& players) {
        vm.selectSlot(0);
        for (const qint64 id : players) {
            ASSERT_TRUE(vm.pickPlayer(id));
        }
    }

    [[nodiscard]] std::vector<qint64> eleven() const {
        std::vector<qint64> ids;
        for (int i = 1; i <= 11; ++i) {
            ids.push_back(player(i));
        }
        return ids;
    }

    [[nodiscard]] static qint64 slotPlayer(const PreMatchViewModel& vm, int slot) {
        return vm.board()->value(slot, "playerId").toLongLong();
    }

    [[nodiscard]] static QStringList ruleKeys(const PreMatchViewModel& vm) {
        QStringList keys;
        for (int row = 0; row < vm.issues()->rowCount(); ++row) {
            keys << vm.issues()->value(row, "ruleKey").toString();
        }
        return keys;
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

TEST_F(PreMatchViewModelTest, OpensTheNextPlannedMatch) {
    const auto vm = open();
    EXPECT_EQ(vm->matchId(), m_match.value);
    EXPECT_TRUE(vm->hasMatch());
    EXPECT_EQ(vm->opponentName(), "Rywal");
    EXPECT_EQ(vm->matchDate(), "2026-11-07");
    EXPECT_EQ(vm->formation(), "4-4-2");
    EXPECT_EQ(vm->board()->rowCount(), 11);
    EXPECT_EQ(vm->players()->rowCount(), kSquad);
    EXPECT_EQ(vm->matches()->rowCount(), 1);
    EXPECT_FALSE(vm->readOnly());
    EXPECT_FALSE(vm->dirty());
    EXPECT_EQ(ruleKeys(*vm), (QStringList{"startingEleven", "goalkeeper"}));
}

TEST_F(PreMatchViewModelTest, KeyboardPathPlacesEleven) {
    const auto vm = open();
    vm->selectSlot(0);
    for (int i = 1; i <= 11; ++i) {
        ASSERT_TRUE(vm->pickPlayer(player(i))) << i;
        // The selection moves on to the next free slot.
        EXPECT_EQ(vm->selectedSlot(), i < 11 ? i : -1);
    }
    EXPECT_EQ(vm->startingCount(), 11);
    for (int slot = 0; slot < 11; ++slot) {
        EXPECT_EQ(slotPlayer(*vm, slot), player(slot + 1));
    }
    EXPECT_TRUE(ruleKeys(*vm).isEmpty());
    EXPECT_EQ(vm->errorCount(), 0);
    EXPECT_TRUE(vm->dirty());
    EXPECT_EQ(vm->players()->value(0, "placement").toString(), "xi");
}

TEST_F(PreMatchViewModelTest, PickingWithoutASelectionFillsTheFirstFreeSlot) {
    const auto vm = open();
    ASSERT_TRUE(vm->pickPlayer(player(5)));
    EXPECT_EQ(slotPlayer(*vm, 0), player(5));
    // Picking a player who is already placed moves them into the selected slot.
    vm->selectSlot(4);
    ASSERT_TRUE(vm->pickPlayer(player(5)));
    EXPECT_EQ(slotPlayer(*vm, 0), 0);
    EXPECT_EQ(slotPlayer(*vm, 4), player(5));
}

TEST_F(PreMatchViewModelTest, SwapAndMoveToTheBench) {
    const auto vm = open();
    pickEleven(*vm, eleven());
    vm->swapSlots(9, 10);
    EXPECT_EQ(slotPlayer(*vm, 9), player(11));
    EXPECT_EQ(slotPlayer(*vm, 10), player(10));
    EXPECT_EQ(vm->board()->value(9, "positionKey").toString(), "st"); // positions stay

    ASSERT_TRUE(vm->moveSlotToBench(6));
    EXPECT_EQ(slotPlayer(*vm, 6), 0);
    EXPECT_EQ(vm->benchCount(), 1);
    EXPECT_EQ(vm->bench()->value(0, "playerId").toLongLong(), player(7));
    EXPECT_EQ(ruleKeys(*vm), (QStringList{"startingEleven"}));

    // A bench player dragged on a slot swaps with the one there.
    ASSERT_TRUE(vm->assignToSlot(5, player(7)));
    EXPECT_EQ(slotPlayer(*vm, 5), player(7));
    EXPECT_EQ(vm->bench()->value(0, "playerId").toLongLong(), player(6));
}

TEST_F(PreMatchViewModelTest, TheBenchHoldsTwelve) {
    const auto vm = open();
    Messages errors(vm.get(), &PreMatchViewModel::errorOccurred);
    for (int i = 1; i <= 12; ++i) {
        ASSERT_TRUE(vm->addToBench(player(i)));
    }
    EXPECT_EQ(vm->benchCount(), vm->maxBench());
    EXPECT_FALSE(vm->addToBench(player(13)));
    EXPECT_EQ(vm->benchCount(), 12);
    ASSERT_EQ(errors.texts().size(), 1);
    EXPECT_TRUE(errors.texts().front().contains("12"));
}

TEST_F(PreMatchViewModelTest, AnInjuredPlayerIsAnErrorThatNamesHim) {
    AvailabilityDraft injury;
    injury.type = AvailabilityType::Injury;
    injury.fromDate = Date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{25}};
    ASSERT_TRUE(registry().squad().addAvailability(m_players[6], injury).has_value());
    const auto vm = open();
    EXPECT_EQ(vm->players()->value(6, "statusKey").toString(), "injured");
    pickEleven(*vm, eleven());
    ASSERT_EQ(ruleKeys(*vm), (QStringList{"unavailable"}));
    EXPECT_TRUE(vm->issues()->value(0, "text").toString().contains("Numer07"));
    EXPECT_EQ(vm->issues()->value(0, "slotIndex").toInt(), 6);
    EXPECT_EQ(vm->board()->value(6, "severity").toString(), "error");

    // Clicking the issue selects the slot; replacing the player removes the error.
    vm->activateIssue(0);
    EXPECT_EQ(vm->selectedSlot(), 6);
    ASSERT_TRUE(vm->pickPlayer(player(12)));
    EXPECT_TRUE(ruleKeys(*vm).isEmpty());
    EXPECT_EQ(vm->board()->value(6, "severity").toString(), "");
}

TEST_F(PreMatchViewModelTest, SaveAndReadTheSame) {
    {
        const auto vm = open();
        Messages messages(vm.get(), &PreMatchViewModel::saved);
        pickEleven(*vm, eleven());
        ASSERT_TRUE(vm->addToBench(player(15)));
        ASSERT_TRUE(vm->setSlotPosition(8, "lw"));
        EXPECT_TRUE(vm->dirty());
        ASSERT_TRUE(vm->save());
        EXPECT_FALSE(vm->dirty());
        EXPECT_TRUE(vm->hasSavedLineup());
        ASSERT_EQ(messages.texts().size(), 1);
        EXPECT_EQ(messages.texts().front(), "Lineup saved.");
    }
    const auto again = open();
    EXPECT_TRUE(again->hasSavedLineup());
    EXPECT_FALSE(again->dirty());
    for (int slot = 0; slot < 11; ++slot) {
        EXPECT_EQ(slotPlayer(*again, slot), player(slot + 1));
    }
    EXPECT_EQ(again->board()->value(8, "positionKey").toString(), "lw");
    EXPECT_EQ(again->formation(), "custom");
    EXPECT_EQ(again->formationText(), "4-3-1-2 (custom)");
    EXPECT_EQ(again->bench()->value(0, "playerId").toLongLong(), player(15));
}

TEST_F(PreMatchViewModelTest, SavingWithErrorsSaysItIsADraft) {
    const auto vm = open();
    Messages messages(vm.get(), &PreMatchViewModel::saved);
    ASSERT_TRUE(vm->pickPlayer(player(1)));
    ASSERT_TRUE(vm->save());
    ASSERT_EQ(messages.texts().size(), 1);
    EXPECT_TRUE(messages.texts().front().contains("draft"));
}

TEST_F(PreMatchViewModelTest, ChangingTheFormationMovesWhoDoesNotFit) {
    const auto vm = open();
    Messages notices(vm.get(), &PreMatchViewModel::notice);
    pickEleven(*vm, eleven());
    vm->setFormation("4-3-3");
    EXPECT_EQ(vm->formation(), "4-3-3");
    EXPECT_EQ(vm->startingCount(), 8);
    EXPECT_EQ(vm->benchCount(), 3); // rm, lm and the second striker
    ASSERT_EQ(notices.texts().size(), 1);
    EXPECT_TRUE(notices.texts().front().contains("Numer06"));
}

// P12 decision 12: the keyboard path of a position change, and a drop in another zone.
TEST_F(PreMatchViewModelTest, PositionOfASlotByKeyboardAndByDrop) {
    const auto vm = open();
    pickEleven(*vm, eleven());
    vm->selectSlot(8);
    EXPECT_EQ(vm->selectedPosition(), "lm");
    ASSERT_TRUE(vm->setSlotPosition(8, "lw"));
    EXPECT_EQ(vm->selectedPosition(), "lw");
    EXPECT_EQ(vm->formation(), "custom");
    EXPECT_EQ(vm->formationOptions().front().toMap().value("value").toString(), "custom");
    // Player 9 is a left midfielder now on the left wing.
    EXPECT_EQ(ruleKeys(*vm), (QStringList{"outOfPosition"}));

    // Dropped back in the left midfield zone (x 0.1, y 0.5): the catalogue shape again.
    ASSERT_TRUE(vm->moveSlotTo(8, 0.1, 0.5));
    EXPECT_EQ(vm->board()->value(8, "positionKey").toString(), "lm");
    EXPECT_EQ(vm->formation(), "4-4-2");
    EXPECT_TRUE(ruleKeys(*vm).isEmpty());

    // At most one goalkeeper.
    Messages errors(vm.get(), &PreMatchViewModel::errorOccurred);
    EXPECT_FALSE(vm->moveSlotTo(3, 0.5, 0.95));
    EXPECT_FALSE(vm->setSlotPosition(3, "gk"));
    EXPECT_EQ(errors.texts().size(), 2);
}

TEST_F(PreMatchViewModelTest, DiscardChanges) {
    const auto vm = open();
    pickEleven(*vm, eleven());
    ASSERT_TRUE(vm->dirty());
    vm->discardChanges();
    EXPECT_FALSE(vm->dirty());
    EXPECT_EQ(vm->startingCount(), 0);
}

TEST_F(PreMatchViewModelTest, OtherRolesSeeTheBoardReadOnly) {
    {
        const auto vm = open();
        pickEleven(*vm, eleven());
        ASSERT_TRUE(vm->save());
    }
    logInAs(Role::Physio);
    const auto vm = open();
    Messages errors(vm.get(), &PreMatchViewModel::errorOccurred);
    EXPECT_TRUE(vm->readOnly());
    EXPECT_FALSE(vm->readOnlyText().isEmpty());
    EXPECT_EQ(vm->startingCount(), 11);
    EXPECT_FALSE(vm->pickPlayer(player(12)));
    EXPECT_FALSE(vm->addToBench(player(12)));
    EXPECT_FALSE(vm->save());
    vm->setFormation("3-5-2");
    EXPECT_EQ(vm->formation(), "4-4-2");
    EXPECT_FALSE(vm->dirty());
    EXPECT_GE(errors.texts().size(), 3);
}

TEST_F(PreMatchViewModelTest, Templates) {
    const auto vm = open();
    Messages taken(vm.get(), &PreMatchViewModel::templateNameTaken);
    Messages deleted(vm.get(), &PreMatchViewModel::templateDeleted);
    pickEleven(*vm, eleven());
    ASSERT_TRUE(vm->saveTemplate("Plan A", false));
    ASSERT_EQ(vm->templates()->rowCount(), 1);
    EXPECT_TRUE(vm->hasTemplates());
    EXPECT_FALSE(vm->saveTemplate("plan a", false));
    EXPECT_EQ(taken.texts(), (QStringList{"plan a"}));
    ASSERT_TRUE(vm->saveTemplate("plan a", true));
    EXPECT_EQ(vm->templates()->rowCount(), 1);

    vm->clearLineup();
    EXPECT_EQ(vm->startingCount(), 0);
    const qint64 id = vm->templates()->value(0, "value").toLongLong();
    ASSERT_TRUE(vm->loadTemplate(id));
    EXPECT_EQ(vm->startingCount(), 11);

    ASSERT_TRUE(vm->deleteTemplate(id));
    EXPECT_EQ(vm->templates()->rowCount(), 0);
    EXPECT_EQ(deleted.texts().size(), 1);
    vm->undoDeleteTemplate();
    EXPECT_EQ(vm->templates()->rowCount(), 1);
}

TEST_F(PreMatchViewModelTest, ExportAndImport) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QUrl file = QUrl::fromLocalFile(dir.filePath("plan.tactic.json"));
    const auto vm = open();
    pickEleven(*vm, eleven());
    ASSERT_TRUE(vm->setSlotPosition(5, "rw"));
    ASSERT_TRUE(vm->exportTactic(file));
    vm->discardChanges();
    ASSERT_EQ(vm->startingCount(), 0);
    ASSERT_TRUE(vm->importTactic(file));
    EXPECT_EQ(vm->startingCount(), 11);
    EXPECT_EQ(vm->board()->value(5, "positionKey").toString(), "rw");
}

TEST_F(PreMatchViewModelTest, LoadStatusesArriveLater) {
    registry().load().holdResults();
    PreMatchViewModel vm;
    vm.setTeamId(m_team.value);
    EXPECT_TRUE(vm.loadPending());
    registry().load().releaseResults();
    ASSERT_TRUE(waitUntil([&] { return !vm.loadPending(); }));
    // Nobody has RPE: grey for everyone, and no high-load warning.
    EXPECT_EQ(vm.players()->value(0, "loadStatusKey").toString(), "notEnoughData");
}

TEST_F(PreMatchViewModelTest, TheThresholdOfYellowCardsIsLive) {
    const auto vm = open();
    pickEleven(*vm, eleven());
    EXPECT_TRUE(ruleKeys(*vm).isEmpty());
    // No cards yet: 0 mod 1… the threshold is at least 2, so still nothing.
    vm->setYellowCardThreshold(2);
    EXPECT_TRUE(ruleKeys(*vm).isEmpty());
    EXPECT_EQ(vm->yellowCardThreshold(), 2);
}

TEST_F(PreMatchViewModelTest, NoPlannedMatchIsAnEmptyState) {
    ASSERT_TRUE(registry().calendar().deleteMatch(m_match).has_value());
    const auto vm = open();
    EXPECT_FALSE(vm->hasMatch());
    EXPECT_FALSE(vm->hasPlannedMatches());
    EXPECT_EQ(vm->board()->rowCount(), 0);
    EXPECT_TRUE(vm->readOnly());
    EXPECT_TRUE(vm->loadError().isEmpty());
}

// P12 decision 3: one threshold, 2–15, default 4, kept in the settings.
TEST_F(PreMatchViewModelTest, TheThresholdSettingIsClampedAndKept) {
    {
        fdss::viewmodels::AppSettings settings;
        settings.setYellowCardThreshold(4);
        EXPECT_EQ(settings.yellowCardThreshold(), 4);
        settings.setYellowCardThreshold(99);
        EXPECT_EQ(settings.yellowCardThreshold(), settings.maxYellowCardThreshold());
        settings.setYellowCardThreshold(1);
        EXPECT_EQ(settings.yellowCardThreshold(), settings.minYellowCardThreshold());
        EXPECT_EQ(settings.minYellowCardThreshold(), 2);
        EXPECT_EQ(settings.maxYellowCardThreshold(), 15);
        settings.setYellowCardThreshold(6);
    }
    fdss::viewmodels::AppSettings again;
    EXPECT_EQ(again.yellowCardThreshold(), 6);
    again.setYellowCardThreshold(kDefaultYellowCardThreshold);
}

// --- Codex review of P12 ---

// The facts cannot be read again: the draft stays with its dirty state, loadError says why, and a
// later reload clears it and keeps the draft.
TEST_F(PreMatchViewModelTest, AFailedReloadKeepsTheDraft) {
    const auto vm = open();
    pickEleven(*vm, eleven());
    ASSERT_TRUE(vm->dirty());
    ASSERT_TRUE(m_db->execSql(QStringLiteral("ALTER TABLE lineup RENAME TO lineup_gone"), "test")
                    .has_value());
    SignalCount dirtyChanges(vm.get(), &PreMatchViewModel::dirtyChanged);
    vm->reload();
    EXPECT_FALSE(vm->loadError().isEmpty());
    EXPECT_TRUE(vm->hasMatch());
    EXPECT_TRUE(vm->dirty());
    EXPECT_EQ(vm->startingCount(), 11);
    EXPECT_EQ(dirtyChanges.count(), 0);

    ASSERT_TRUE(m_db->execSql(QStringLiteral("ALTER TABLE lineup_gone RENAME TO lineup"), "test")
                    .has_value());
    vm->reload();
    ASSERT_TRUE(waitUntil([&] { return !vm->loadPending(); }));
    EXPECT_TRUE(vm->loadError().isEmpty());
    EXPECT_TRUE(vm->dirty());
    EXPECT_EQ(vm->startingCount(), 11);
}

// The load cannot be read: an explicit incomplete check, never "ready", and a retry on reload.
TEST_F(PreMatchViewModelTest, AFailedLoadCheckIsNeverReady) {
    // A finished match makes LoadService read the appearances, which Pre-match does not need.
    MatchDraft past;
    past.opponentId = registry().calendar().listOpponents().value().front().id;
    past.date = Date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{10}};
    const auto pastMatch = registry().calendar().addMatch(m_team, past);
    ASSERT_TRUE(pastMatch.has_value());
    ASSERT_TRUE(fdss::data::MatchRepository(*m_db)
                    .setResult(*pastMatch, 1, 0, MatchStatus::Finished)
                    .has_value());
    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE appearance RENAME TO appearance_gone"), "test")
            .has_value());
    const auto vm = open();
    pickEleven(*vm, eleven());
    EXPECT_TRUE(vm->loadCheckFailed());
    EXPECT_FALSE(vm->loadCheckText().isEmpty());
    EXPECT_EQ(vm->errorCount(), 0);
    EXPECT_NE(vm->issuesSummary(), "No issues: the lineup is ready for Match Day.");
    EXPECT_TRUE(vm->players()->value(0, "detailsText").toString().contains("could not be checked"));

    ASSERT_TRUE(
        m_db->execSql(QStringLiteral("ALTER TABLE appearance_gone RENAME TO appearance"), "test")
            .has_value());
    vm->reload();
    ASSERT_TRUE(waitUntil([&] { return !vm->loadPending(); }));
    EXPECT_FALSE(vm->loadCheckFailed());
    EXPECT_TRUE(vm->loadCheckText().isEmpty());
    EXPECT_EQ(vm->issuesSummary(), "No issues: the lineup is ready for Match Day.");
}

// Clear, template and import replace the lineup; undo brings back the one before, dirty included.
TEST_F(PreMatchViewModelTest, ReplacingTheLineupCanBeUndone) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QUrl file = QUrl::fromLocalFile(dir.filePath("plan.tactic.json"));
    const auto vm = open();
    Messages replaced(vm.get(), &PreMatchViewModel::lineupReplaced);
    // A saved lineup, and a template and a file of another one.
    pickEleven(*vm, eleven());
    ASSERT_TRUE(vm->save());
    vm->setFormation("3-5-2");
    ASSERT_TRUE(vm->saveTemplate("Plan B", false));
    ASSERT_TRUE(vm->exportTactic(file));
    vm->discardChanges();
    ASSERT_FALSE(vm->dirty());
    const std::vector<qint64> saved = [&] {
        std::vector<qint64> ids;
        for (int slot = 0; slot < 11; ++slot) {
            ids.push_back(slotPlayer(*vm, slot));
        }
        return ids;
    }();
    const auto expectSaved = [&](const char* what) {
        EXPECT_FALSE(vm->dirty()) << what;
        EXPECT_EQ(vm->formation(), "4-4-2") << what;
        for (int slot = 0; slot < 11; ++slot) {
            EXPECT_EQ(slotPlayer(*vm, slot), saved[static_cast<std::size_t>(slot)]) << what;
        }
    };

    vm->clearLineup();
    EXPECT_EQ(vm->startingCount(), 0);
    EXPECT_TRUE(vm->dirty());
    vm->undoReplace();
    expectSaved("clear");

    ASSERT_TRUE(vm->loadTemplate(vm->templates()->value(0, "value").toLongLong()));
    EXPECT_EQ(vm->formation(), "3-5-2");
    vm->undoReplace();
    expectSaved("template");

    ASSERT_TRUE(vm->importTactic(file));
    EXPECT_EQ(vm->formation(), "3-5-2");
    vm->undoReplace();
    expectSaved("import");

    EXPECT_EQ(replaced.texts().size(), 3);
    // Nothing more to undo.
    vm->undoReplace();
    expectSaved("second undo");
}

// The threshold is saved only with ManageSettings, checked in C++ (not only by the view).
TEST_F(PreMatchViewModelTest, OnlyManageSettingsChangesTheThreshold) {
    logInAs(Role::Physio);
    fdss::viewmodels::AppSettings settings;
    const int before = settings.yellowCardThreshold();
    int errors = 0;
    QObject::connect(&settings, &fdss::viewmodels::AppSettings::errorOccurred,
                     [&errors](const QString&) { ++errors; });
    EXPECT_FALSE(settings.setYellowCardThreshold(before == 6 ? 7 : 6));
    EXPECT_EQ(settings.yellowCardThreshold(), before);
    EXPECT_EQ(errors, 1);
}

TEST_F(PreMatchViewModelTest, AThresholdChangeUpdatesTheIssues) {
    const auto vm = open();
    pickEleven(*vm, eleven());
    SignalCount issues(vm.get(), &PreMatchViewModel::issuesChanged);
    vm->setYellowCardThreshold(3);
    EXPECT_GE(issues.count(), 1);
}

// The tooltip of a row: availability on the match day, the load as of its day.
TEST_F(PreMatchViewModelTest, PlayerDetailsNameTheDays) {
    const auto vm = open();
    const QString details = vm->players()->value(0, "detailsText").toString();
    EXPECT_TRUE(details.contains("On the match day:")) << details.toStdString();
    // No RPE in this club: today's grey status is as of yesterday (P11 decision 4).
    EXPECT_TRUE(details.contains("Load as of")) << details.toStdString();
}

// The picker of a centre back: CB as the main position, then as a second one, then the others; the
// unavailable ones last in their group (P12 manual test, decision 20).
TEST_F(PreMatchViewModelTest, ThePickerOfACentreBackGroupsByPosition) {
    auto secondary = registry().squad().player(m_players[11]);
    ASSERT_TRUE(secondary.has_value());
    PlayerDraft draft = secondary->player;
    draft.secondaryPositions = {Position::Cb};
    ASSERT_TRUE(registry().squad().updatePlayer(m_players[11], draft).has_value());
    AvailabilityDraft injury;
    injury.type = AvailabilityType::Injury;
    injury.fromDate = Date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{25}};
    ASSERT_TRUE(registry().squad().addAvailability(m_players[2], injury).has_value());
    ASSERT_TRUE(registry().squad().addAvailability(m_players[12], injury).has_value());
    const auto vm = open();
    ASSERT_TRUE(vm->assignToSlot(2, player(4)));

    ASSERT_EQ(vm->board()->value(2, "positionKey").toString(), "cb");
    ASSERT_TRUE(vm->openPicker(2));
    EXPECT_EQ(vm->pickerSlot(), 2);
    EXPECT_EQ(vm->selectedSlot(), 2);
    const auto* picker = vm->slotPicker();
    ASSERT_EQ(picker->rowCount(), kSquad);
    std::vector<qint64> order;
    QStringList groups;
    for (int row = 0; row < picker->rowCount(); ++row) {
        order.push_back(picker->value(row, "playerId").toLongLong());
        const QString group = picker->value(row, "group").toString();
        if (groups.isEmpty() || groups.back() != group) {
            groups << group;
        }
    }
    EXPECT_EQ(groups, (QStringList{"primary", "secondary", "other"}));
    // Player 3 is injured: last of the centre backs.
    EXPECT_EQ(order[0], player(4));
    EXPECT_EQ(order[1], player(3));
    EXPECT_FALSE(picker->value(1, "available").toBool());
    EXPECT_TRUE(picker->value(0, "current").toBool());
    EXPECT_EQ(picker->value(0, "placement").toString(), "xi");
    EXPECT_EQ(order[2], player(12));
    EXPECT_EQ(order[3], player(1));
    // Player 13 is injured: last of the others.
    EXPECT_EQ(order.back(), player(13));
    EXPECT_EQ(picker->value(kSquad - 1, "statusKey").toString(), "injured");

    // A pick swaps by the rules of assignToSlot and the picker follows.
    ASSERT_TRUE(vm->pickForSlot(player(12)));
    EXPECT_EQ(slotPlayer(*vm, 2), player(12));
    EXPECT_TRUE(picker->value(2, "current").toBool());
    EXPECT_TRUE(vm->pickForSlot(player(12)));
    vm->closePicker();
    EXPECT_EQ(vm->pickerSlot(), -1);
    EXPECT_EQ(picker->rowCount(), 0);
}

// The squad list: the XI from the goalkeeper up the pitch, the bench in its order, then the rest by
// the chosen column, rows without a value last both ways (P12 manual test, decision 21).
TEST_F(PreMatchViewModelTest, TheSquadListPutsTheLineupFirst) {
    PlayerDraft unnumbered;
    unnumbered.firstName = "Bez";
    unnumbered.lastName = "Abacki";
    ASSERT_TRUE(registry().squad().addPlayer(m_team, unnumbered).has_value());
    const auto vm = open();
    auto* list = vm->sortedPlayers();
    ASSERT_EQ(list->rowCount(), kSquad + 1);
    // Strikers in slot 0 and 1, the goalkeeper in slot 10: the goalkeeper still comes first.
    ASSERT_TRUE(vm->assignToSlot(0, player(10)));
    ASSERT_TRUE(vm->setSlotPosition(0, "st"));
    ASSERT_TRUE(vm->setSlotPosition(10, "gk"));
    ASSERT_TRUE(vm->assignToSlot(10, player(1)));
    ASSERT_TRUE(vm->addToBench(player(20)));
    ASSERT_TRUE(vm->addToBench(player(15)));

    EXPECT_EQ(list->value(0, "playerId").toLongLong(), player(1));
    EXPECT_EQ(list->value(0, "section").toInt(), 0);
    EXPECT_EQ(list->value(1, "playerId").toLongLong(), player(10));
    EXPECT_EQ(list->value(2, "playerId").toLongLong(), player(20));
    EXPECT_EQ(list->value(2, "section").toInt(), 1);
    EXPECT_EQ(list->value(3, "playerId").toLongLong(), player(15));
    EXPECT_EQ(list->value(4, "section").toInt(), 2);
    EXPECT_EQ(list->value(4, "playerId").toLongLong(), player(2));
    EXPECT_EQ(list->value(kSquad, "shortName").toString(), "Abacki");

    // Descending by number: the lineup stays on top, the player without a number stays last.
    list->sortBy("number");
    EXPECT_FALSE(list->ascending());
    EXPECT_EQ(list->value(0, "playerId").toLongLong(), player(1));
    EXPECT_EQ(list->value(4, "playerId").toLongLong(), player(19));
    EXPECT_EQ(list->value(kSquad, "shortName").toString(), "Abacki");

    // By name, then descending: a surname always has a value, so it sorts like the others.
    list->sortBy("name");
    EXPECT_TRUE(list->ascending());
    EXPECT_EQ(list->value(4, "shortName").toString(), "Abacki");
    list->sortBy("name");
    EXPECT_EQ(list->value(4, "playerId").toLongLong(), player(19));
    EXPECT_EQ(list->value(kSquad, "shortName").toString(), "Abacki");
    EXPECT_EQ(list->rowOf(player(1)), 0);
}

// P19 decisions 6 and 20 of P12: a player without a position gets a note instead of the
// out-of-position warning, and the picker lists them last, under "No position".
TEST_F(PreMatchViewModelTest, APlayerWithoutAPositionGetsANoteAndTheLastPickerGroup) {
    auto none = registry().squad().player(m_players[13]);
    ASSERT_TRUE(none.has_value());
    PlayerDraft draft = none->player;
    draft.primaryPosition = std::nullopt;
    draft.secondaryPositions.clear();
    ASSERT_TRUE(registry().squad().updatePlayer(m_players[13], draft).has_value());
    const auto vm = open();

    ASSERT_TRUE(vm->assignToSlot(2, player(14)));

    EXPECT_EQ(vm->board()->value(2, "severity").toString(), "info");
    EXPECT_FALSE(vm->board()->value(2, "issueText").toString().isEmpty());
    EXPECT_EQ(vm->warningCount(), 0);
    ASSERT_TRUE(vm->openPicker(2));
    const auto* picker = vm->slotPicker();
    ASSERT_GT(picker->rowCount(), 0);
    EXPECT_EQ(picker->value(picker->rowCount() - 1, "group").toString(), "none");
    EXPECT_EQ(picker->value(picker->rowCount() - 1, "playerId").toLongLong(), player(14));
    EXPECT_NE(picker->value(picker->rowCount() - 1, "groupText").toString(),
              picker->value(0, "groupText").toString());
}
