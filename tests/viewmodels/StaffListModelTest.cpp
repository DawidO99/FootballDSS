#include "viewmodels/StaffListModel.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <gtest/gtest.h>

using namespace fdss::core;
using fdss::viewmodels::StaffListModel;

namespace {

StaffUser user(std::int64_t id, const char* name, Role role, bool active = true) {
    StaffUser result;
    result.id = StaffUserId{id};
    result.name = name;
    result.role = role;
    result.active = active;
    return result;
}

} // namespace

TEST(StaffListModelTest, InitialsAreTheFirstLettersOfTheFirstAndTheLastWord) {
    EXPECT_EQ(StaffListModel::initials(QStringLiteral("Jan Kowalski")), QStringLiteral("JK"));
    EXPECT_EQ(StaffListModel::initials(QStringLiteral("Anna Maria Nowak")), QStringLiteral("AN"));
    EXPECT_EQ(StaffListModel::initials(QStringLiteral("Trener")), QStringLiteral("T"));
}

TEST(StaffListModelTest, InitialsHandlePolishLettersCaseAndSpaces) {
    EXPECT_EQ(StaffListModel::initials(QStringLiteral("łukasz żak")), QStringLiteral("ŁŻ"));
    EXPECT_EQ(StaffListModel::initials(QStringLiteral("  Jan   Kowalski ")), QStringLiteral("JK"));
    EXPECT_TRUE(StaffListModel::initials(QStringLiteral("   ")).isEmpty());
    EXPECT_TRUE(StaffListModel::initials({}).isEmpty());
}

TEST(StaffListModelTest, InitialsKeepALetterOfTwoCodeUnitsWhole) {
    const QString letter = QString::fromUcs4(U"\U0001D49C");

    EXPECT_EQ(StaffListModel::initials(letter + QStringLiteral("bc")), letter);
}

TEST(StaffListModelTest, RowsCarryTheNamedRoles) {
    StaffListModel model;
    model.setStaff({user(7, "Jan Kowalski", Role::Physio, false)});

    ASSERT_EQ(model.rowCount(), 1);
    const QModelIndex row = model.index(0);
    EXPECT_EQ(model.data(row, StaffListModel::StaffIdRole).toLongLong(), 7);
    EXPECT_EQ(model.data(row, StaffListModel::NameRole).toString(), QStringLiteral("Jan Kowalski"));
    EXPECT_EQ(model.data(row, StaffListModel::RoleKeyRole).toString(), QStringLiteral("physio"));
    EXPECT_EQ(model.data(row, StaffListModel::RoleNameRole).toString(), QStringLiteral("Physio"));
    EXPECT_EQ(model.data(row, StaffListModel::InitialsRole).toString(), QStringLiteral("JK"));
    EXPECT_FALSE(model.data(row, StaffListModel::ActiveRole).toBool());
}

TEST(StaffListModelTest, EveryRoleHasAName) {
    const QHash<int, QByteArray> names = StaffListModel().roleNames();

    for (const char* name : {"staffId", "name", "roleKey", "roleName", "initials", "active"}) {
        EXPECT_TRUE(names.values().contains(QByteArray(name))) << name;
    }
}

TEST(StaffListModelTest, FindReturnsTheUserOrNothing) {
    StaffListModel model;
    model.setStaff({user(7, "Jan", Role::Coach)});

    ASSERT_NE(model.find(7), nullptr);
    EXPECT_EQ(model.find(7)->name, "Jan");
    EXPECT_EQ(model.find(8), nullptr);
}

TEST(StaffListModelTest, RowAtAndRowOfForTheSelectionOfATable) {
    StaffListModel model;
    model.setStaff({user(1, "Jan", Role::Coach), user(7, "Anna", Role::Analyst, false)});

    const QVariantMap row = model.rowAt(1);
    EXPECT_EQ(row.value(QStringLiteral("staffId")).toLongLong(), 7);
    EXPECT_EQ(row.value(QStringLiteral("name")).toString(), QStringLiteral("Anna"));
    EXPECT_EQ(row.value(QStringLiteral("roleKey")).toString(), QStringLiteral("analyst"));
    EXPECT_FALSE(row.value(QStringLiteral("active")).toBool());
    EXPECT_TRUE(model.rowAt(-1).isEmpty());
    EXPECT_TRUE(model.rowAt(2).isEmpty());

    EXPECT_EQ(model.rowOf(7), 1);
    EXPECT_EQ(model.rowOf(8), -1);
}

TEST(StaffListModelTest, ALanguageChangeUpdatesOnlyTheRoleNames) {
    StaffListModel model;
    model.setStaff({user(1, "Jan", Role::Coach), user(2, "Anna", Role::Analyst)});
    int changes = 0;
    int lastRow = -1;
    QList<int> roles;
    QObject::connect(
        &model, &StaffListModel::dataChanged, &model,
        [&](const QModelIndex& first, const QModelIndex& last, const QList<int>& list) {
            ++changes;
            EXPECT_EQ(first.row(), 0);
            lastRow = last.row();
            roles = list;
        });

    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &event);

    EXPECT_EQ(changes, 1);
    EXPECT_EQ(lastRow, 1);
    EXPECT_EQ(roles, QList<int>{StaffListModel::RoleNameRole});
}

TEST(StaffListModelTest, ALanguageChangeOnAnEmptyListSignalsNothing) {
    StaffListModel model;
    int changes = 0;
    QObject::connect(&model, &StaffListModel::dataChanged, &model, [&] { ++changes; });

    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &event);

    EXPECT_EQ(changes, 0);
}
