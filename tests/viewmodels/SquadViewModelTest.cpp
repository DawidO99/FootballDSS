#include "viewmodels/SquadViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Availability.hpp"
#include "core/domain/Player.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/SquadListModel.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QStringList>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::SquadListModel;
using fdss::viewmodels::SquadViewModel;

namespace {

constexpr Date kLastMonth{std::chrono::year{2026}, std::chrono::October, std::chrono::day{1}};

class SquadViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
    }

    PlayerId addPlayer(const char* firstName, const char* lastName, std::optional<int> number,
                       Position position, std::optional<int> birthYear = std::nullopt,
                       std::vector<Position> secondary = {}) {
        PlayerDraft draft;
        draft.firstName = firstName;
        draft.lastName = lastName;
        draft.shirtNumber = number;
        draft.primaryPosition = position;
        draft.birthYear = birthYear;
        draft.secondaryPositions = std::move(secondary);
        const auto id = registry().squad().addPlayer(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(PlayerId{});
    }

    void addInjury(PlayerId player) {
        AvailabilityDraft draft;
        draft.fromDate = kLastMonth;
        ASSERT_TRUE(registry().squad().addAvailability(player, draft).has_value());
    }

    // Kowalski GK 1 (2000), Łukasiewicz CB 4 (1990, injured), Nowak ST no number (no year), Żak CM
    // 8 (1995), Adamski ST 9 (archived).
    void seedSquad() {
        addPlayer("Jan", "Kowalski", 1, Position::Gk, 2000);
        const PlayerId injured = addPlayer("Łukasz", "Łukasiewicz", 4, Position::Cb, 1990);
        addInjury(injured);
        addPlayer("Adam", "Nowak", std::nullopt, Position::St);
        addPlayer("Piotr", "Żak", 8, Position::Cm, 1995);
        const PlayerId archived = addPlayer("Marek", "Adamski", 9, Position::St, 1985);
        ASSERT_TRUE(registry().squad().archivePlayer(archived).has_value());
    }

    void open(SquadViewModel& squad) const { squad.setTeamId(m_team.value); }

    // The last names in the order of the table.
    [[nodiscard]] static QStringList lastNames(const SquadViewModel& squad) {
        QStringList names;
        for (int row = 0; row < squad.players()->rowCount(); ++row) {
            names.append(squad.rowAt(row)
                             .value(QStringLiteral("name"))
                             .toString()
                             .section(QLatin1Char(' '), -1));
        }
        return names;
    }

    TeamId m_team;
};

const auto kAvailable = QStringLiteral("available");
const auto kUnavailable = QStringLiteral("unavailable");
const auto kDefenders = QStringLiteral("defender");

QStringList names(std::initializer_list<const char*> values) {
    QStringList result;
    for (const char* value : values) {
        result.append(QString::fromUtf8(value));
    }
    return result;
}

TEST_F(SquadViewModelTest, WithoutATeamTheListIsEmptyAndQuiet) {
    seedSquad();
    SquadViewModel squad;
    EXPECT_EQ(squad.players()->rowCount(), 0);
    EXPECT_TRUE(squad.loadError().isEmpty());
}

TEST_F(SquadViewModelTest, ByDefaultActivePlayersSortedByNumberWithNoNumberLast) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    EXPECT_EQ(lastNames(squad), names({"Kowalski", "Łukasiewicz", "Żak", "Nowak"}));
    EXPECT_EQ(squad.sortRole(), QStringLiteral("number"));
    EXPECT_TRUE(squad.sortAscending());
}

TEST_F(SquadViewModelTest, RowShowsItsValues) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    const QVariantMap row = squad.rowAt(1);
    EXPECT_EQ(row.value(QStringLiteral("number")).toInt(), 4);
    EXPECT_EQ(row.value(QStringLiteral("position")).toString(), QStringLiteral("CB"));
    EXPECT_EQ(row.value(QStringLiteral("positionTooltip")).toString(),
              QStringLiteral("Centre back"));
    EXPECT_TRUE(row.value(QStringLiteral("secondaryPositions")).toList().isEmpty());
    EXPECT_EQ(row.value(QStringLiteral("age")).toInt(), 2026 - 1990);
    EXPECT_EQ(row.value(QStringLiteral("statusKey")).toString(), QStringLiteral("injured"));

    EXPECT_FALSE(squad.rowAt(3).value(QStringLiteral("number")).isValid());
    EXPECT_FALSE(squad.rowAt(3).value(QStringLiteral("age")).isValid());
    EXPECT_TRUE(squad.rowAt(99).isEmpty());
}

TEST_F(SquadViewModelTest, NoNumberAndNoAgeStayLastInBothDirections) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    squad.sortBy(QStringLiteral("number"), false);
    EXPECT_EQ(lastNames(squad), names({"Żak", "Łukasiewicz", "Kowalski", "Nowak"}));
    squad.sortBy(QStringLiteral("age"), true);
    EXPECT_EQ(lastNames(squad), names({"Kowalski", "Żak", "Łukasiewicz", "Nowak"}));
    squad.sortBy(QStringLiteral("age"), false);
    EXPECT_EQ(lastNames(squad), names({"Łukasiewicz", "Żak", "Kowalski", "Nowak"}));
}

TEST_F(SquadViewModelTest, SortsNamesInPolishOrderByLastName) {
    seedSquad();
    addPlayer("Anna", "Lis", 20, Position::Cm);
    addPlayer("Ewa", "Zając", 21, Position::Cm);
    SquadViewModel squad;
    open(squad);
    squad.sortBy(QStringLiteral("name"), true);
    EXPECT_EQ(lastNames(squad), names({"Kowalski", "Lis", "Łukasiewicz", "Nowak", "Zając", "Żak"}));
}

TEST_F(SquadViewModelTest, SortsPositionsAndStatusesInTheOrderOfTheEnumerations) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    squad.sortBy(QStringLiteral("position"), true);
    EXPECT_EQ(lastNames(squad), names({"Kowalski", "Łukasiewicz", "Żak", "Nowak"}));
    squad.sortBy(QStringLiteral("position"), false);
    EXPECT_EQ(lastNames(squad), names({"Nowak", "Żak", "Łukasiewicz", "Kowalski"}));
    squad.sortBy(QStringLiteral("status"), false);
    EXPECT_EQ(lastNames(squad).first(), QStringLiteral("Łukasiewicz"));
}

TEST_F(SquadViewModelTest, UnknownSortRoleChangesNothing) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    squad.sortBy(QStringLiteral("nonsense"), false);
    EXPECT_EQ(squad.sortRole(), QStringLiteral("number"));
}

TEST_F(SquadViewModelTest, SearchFindsNameWithoutRegardToCaseOrPolishLetters) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    squad.setSearchText(QStringLiteral("LUKASZ"));
    EXPECT_EQ(lastNames(squad), names({"Łukasiewicz"}));
    squad.setSearchText(QStringLiteral("zak"));
    EXPECT_EQ(lastNames(squad), names({"Żak"}));
    squad.setSearchText(QStringLiteral("kowalski jan"));
    EXPECT_EQ(lastNames(squad), names({"Kowalski"}));
    EXPECT_TRUE(squad.filtersActive());
    squad.setSearchText(QStringLiteral("nikt"));
    EXPECT_EQ(squad.players()->rowCount(), 0);
}

TEST_F(SquadViewModelTest, SearchWithDigitsIsExactlyThatNumber) {
    seedSquad();
    addPlayer("Anna", "Lis", 14, Position::Cm);
    SquadViewModel squad;
    open(squad);
    squad.setSearchText(QStringLiteral("4"));
    EXPECT_EQ(lastNames(squad), names({"Łukasiewicz"}));
    squad.setSearchText(QStringLiteral("04"));
    EXPECT_EQ(lastNames(squad), names({"Łukasiewicz"}));
}

TEST_F(SquadViewModelTest, StatusAndPositionGroupsFilter) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    squad.setStatusGroup(kUnavailable);
    EXPECT_EQ(lastNames(squad), names({"Łukasiewicz"}));
    squad.setStatusGroup(kAvailable);
    EXPECT_EQ(lastNames(squad), names({"Kowalski", "Żak", "Nowak"}));
    squad.setPositionGroup(QStringLiteral("forward"));
    EXPECT_EQ(lastNames(squad), names({"Nowak"}));
    squad.setPositionGroup(QStringLiteral("nonsense"));
    EXPECT_EQ(squad.positionGroup(), QStringLiteral("forward"));
}

TEST_F(SquadViewModelTest, ASecondaryPositionPassesTheGroupFilterBehindThePrimaryOnes) {
    seedSquad();
    // A lower number than the centre backs, and yet behind them.
    addPlayer("Ewa", "Zając", 2, Position::Cm, std::nullopt, {Position::Cb});
    // No number at all, and yet in front of the player who only can play there.
    addPlayer("Anna", "Lis", std::nullopt, Position::Lb);
    SquadViewModel squad;
    open(squad);

    squad.setPositionGroup(kDefenders);
    EXPECT_EQ(lastNames(squad), names({"Łukasiewicz", "Lis", "Zając"}));

    squad.setPositionGroup(QStringLiteral("midfielder"));
    EXPECT_EQ(lastNames(squad), names({"Zając", "Żak"}));

    squad.setPositionGroup(QStringLiteral("all"));
    EXPECT_EQ(lastNames(squad), names({"Kowalski", "Zając", "Łukasiewicz", "Żak", "Nowak", "Lis"}));
}

TEST_F(SquadViewModelTest, OnlyTheDefaultOrderPutsSecondaryPositionsBehind) {
    addPlayer("Adam", "Bak", 5, Position::Cb);
    addPlayer("Ewa", "Abel", 2, Position::Cm, std::nullopt, {Position::Cb});
    addPlayer("Jan", "Cis", 9, Position::Lb);
    SquadViewModel squad;
    open(squad);
    squad.setPositionGroup(kDefenders);
    EXPECT_EQ(lastNames(squad), names({"Bak", "Cis", "Abel"}));

    // By the column of the position: still the primary position.
    squad.sortBy(QStringLiteral("position"), true);
    EXPECT_EQ(lastNames(squad), names({"Bak", "Cis", "Abel"}));
    squad.sortBy(QStringLiteral("position"), false);
    EXPECT_EQ(lastNames(squad), names({"Abel", "Cis", "Bak"}));

    // A column the user chose, the number descending included, is that column alone.
    squad.sortBy(QStringLiteral("name"), true);
    EXPECT_EQ(lastNames(squad), names({"Abel", "Bak", "Cis"}));
    squad.sortBy(QStringLiteral("number"), false);
    EXPECT_EQ(lastNames(squad), names({"Cis", "Bak", "Abel"}));

    squad.sortBy(QStringLiteral("number"), true);
    EXPECT_EQ(lastNames(squad), names({"Bak", "Cis", "Abel"}));
}

TEST_F(SquadViewModelTest, TheActiveGroupHighlightsItsSecondaryCodes) {
    const PlayerId id =
        addPlayer("Ewa", "Zając", 2, Position::Cm, std::nullopt, {Position::Cb, Position::St});
    SquadViewModel squad;
    open(squad);
    const auto codes = [&] {
        QStringList result;
        const QVariantList list =
            squad.rowAt(squad.rowOf(id.value)).value(QStringLiteral("secondaryPositions")).toList();
        for (const QVariant& code : list) {
            const QVariantMap map = code.toMap();
            result.append(map.value(QStringLiteral("code")).toString() +
                          (map.value(QStringLiteral("highlighted")).toBool() ? u'+' : u'-'));
        }
        return result;
    };
    EXPECT_EQ(codes(), names({"CB-", "ST-"}));

    SignalCount changed(squad.players(), &QAbstractItemModel::dataChanged);
    squad.setPositionGroup(kDefenders);
    EXPECT_EQ(codes(), names({"CB+", "ST-"}));
    EXPECT_GE(changed.count(), 1);

    squad.setPositionGroup(QStringLiteral("forward"));
    EXPECT_EQ(codes(), names({"CB-", "ST+"}));

    squad.setPositionGroup(QStringLiteral("all"));
    EXPECT_EQ(codes(), names({"CB-", "ST-"}));
}

TEST_F(SquadViewModelTest, TheTooltipSaysWhyTheRowIsInTheGroup) {
    const PlayerId mixed =
        addPlayer("Ewa", "Zając", 2, Position::Cm, std::nullopt, {Position::Cb, Position::St});
    const PlayerId defender =
        addPlayer("Jan", "Cis", 9, Position::Lb, std::nullopt, {Position::Cb});
    const PlayerId plain = addPlayer("Adam", "Bak", 5, Position::Cb);
    SquadViewModel squad;
    open(squad);
    const auto tooltip = [&](PlayerId id) {
        return squad.rowAt(squad.rowOf(id.value))
            .value(QStringLiteral("positionTooltip"))
            .toString();
    };
    EXPECT_EQ(tooltip(mixed),
              QStringLiteral("Central midfielder. Secondary: Centre back, Striker"));
    EXPECT_EQ(tooltip(plain), QStringLiteral("Centre back"));

    squad.setPositionGroup(kDefenders);
    EXPECT_EQ(tooltip(mixed), QStringLiteral("Secondary position: Centre back"));
    // In the group by the primary position: nothing to explain, even with a code highlighted.
    EXPECT_EQ(tooltip(defender), QStringLiteral("Left back. Secondary: Centre back"));
    EXPECT_EQ(tooltip(plain), QStringLiteral("Centre back"));

    squad.setPositionGroup(QStringLiteral("all"));
    EXPECT_EQ(tooltip(mixed),
              QStringLiteral("Central midfielder. Secondary: Centre back, Striker"));
}

TEST_F(SquadViewModelTest, FilterSearchAndSortWorkTogether) {
    seedSquad();
    addPlayer("Anna", "Kowalska", 20, Position::Dm);
    addPlayer("Ewa", "Kowalczyk", 21, Position::Am);
    addPlayer("Maria", "Kowal", 22, Position::St);
    SquadViewModel squad;
    open(squad);
    squad.setSearchText(QStringLiteral("kowal"));
    squad.setStatusGroup(kAvailable);
    squad.setPositionGroup(QStringLiteral("midfielder"));
    squad.sortBy(QStringLiteral("name"), false);
    EXPECT_EQ(lastNames(squad), names({"Kowalska", "Kowalczyk"}));

    squad.clearFilters();
    EXPECT_FALSE(squad.filtersActive());
    EXPECT_EQ(squad.players()->rowCount(), 7);
    EXPECT_EQ(squad.sortRole(), QStringLiteral("name"));
}

TEST_F(SquadViewModelTest, ArchivedPlayersShowOnlyOnRequestAndOnlyOnAll) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    squad.setShowArchived(true);
    EXPECT_EQ(squad.players()->rowCount(), 5);
    const QVariantMap archived = squad.rowAt(3);
    EXPECT_EQ(archived.value(QStringLiteral("name")).toString(), QStringLiteral("Marek Adamski"));
    EXPECT_EQ(archived.value(QStringLiteral("statusKey")).toString(), QStringLiteral("archived"));
    EXPECT_TRUE(archived.value(QStringLiteral("archived")).toBool());

    squad.setStatusGroup(kAvailable);
    EXPECT_FALSE(lastNames(squad).contains(QStringLiteral("Adamski")));
    squad.setStatusGroup(kUnavailable);
    EXPECT_FALSE(lastNames(squad).contains(QStringLiteral("Adamski")));

    squad.clearFilters();
    EXPECT_TRUE(squad.showArchived());
    EXPECT_FALSE(squad.filtersActive());
}

TEST_F(SquadViewModelTest, ArchiveAndUnarchiveChangeTheList) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    const qint64 id = squad.rowAt(0).value(QStringLiteral("playerId")).toLongLong();
    SignalCount reloaded(&squad, &SquadViewModel::playersReloaded);

    ASSERT_TRUE(squad.archive(id));
    EXPECT_EQ(squad.rowOf(id), -1);
    EXPECT_EQ(reloaded.count(), 1);

    ASSERT_TRUE(squad.unarchive(id));
    EXPECT_EQ(squad.rowOf(id), 0);
}

TEST_F(SquadViewModelTest, UnarchiveOntoATakenNumberSaysWhy) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    const qint64 id = squad.rowAt(0).value(QStringLiteral("playerId")).toLongLong();
    ASSERT_TRUE(squad.archive(id));
    addPlayer("Nowy", "Bramkarz", 1, Position::Gk);
    squad.reload();
    SignalCount errors(&squad, &SquadViewModel::errorOccurred);

    EXPECT_FALSE(squad.unarchive(id));
    EXPECT_EQ(errors.count(), 1);
}

TEST_F(SquadViewModelTest, RoleWithoutEditSquadIsRefusedBeforeTheService) {
    seedSquad();
    const StaffUserId physio = addStaff("Fizjo", Role::Physio);
    registry().auth().logout();
    ASSERT_TRUE(registry().auth().login(physio, kPin).has_value());

    SquadViewModel squad;
    open(squad);
    EXPECT_EQ(squad.players()->rowCount(), 4);
    SignalCount errors(&squad, &SquadViewModel::errorOccurred);
    const qint64 id = squad.rowAt(0).value(QStringLiteral("playerId")).toLongLong();
    EXPECT_FALSE(squad.archive(id));
    EXPECT_EQ(errors.count(), 1);
    EXPECT_EQ(squad.rowOf(id), 0);
}

TEST_F(SquadViewModelTest, UnreadableListGivesALoadError) {
    seedSquad();
    SquadViewModel squad;
    open(squad);
    ASSERT_EQ(squad.players()->rowCount(), 4);
    ASSERT_TRUE(m_db->execSql(QStringLiteral("ALTER TABLE player RENAME TO player_gone"),
                              "break the database")
                    .has_value());

    squad.reload();
    EXPECT_FALSE(squad.loadError().isEmpty());
    EXPECT_EQ(squad.players()->rowCount(), 0);
}

} // namespace

// Qt sends the LanguageChange of the application on to every window and item, which the
// application-wide filter sees too: only the application's own one counts, or a language switch
// re-reads every list once per item of the window.
TEST_F(SquadViewModelTest, ReactsOnlyToTheLanguageChangeOfTheApplication) {
    SquadViewModel squad;
    SignalCount options(&squad, &SquadViewModel::optionsChanged);
    QObject item;
    QEvent forwarded(QEvent::LanguageChange);
    QCoreApplication::sendEvent(&item, &forwarded);
    EXPECT_EQ(options.count(), 0);

    QEvent own(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &own);
    EXPECT_EQ(options.count(), 1);
}
