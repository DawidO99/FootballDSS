#include "viewmodels/StaffViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "services/Session.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QVariantMap>

#include <gtest/gtest.h>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::services::PinCheckStatus;
using fdss::viewmodels::StaffListModel;
using fdss::viewmodels::StaffViewModel;

namespace {

const auto kNewPin = QStringLiteral("5678");

class StaffViewModelTest : public ViewModelTest {
protected:
    // A Coach, logged in.
    void SetUp() override {
        ViewModelTest::SetUp();
        m_coach = completeSetup().id;
    }

    StaffUserId m_coach;
};

} // namespace

TEST_F(StaffViewModelTest, ListsAllStaffIncludingTheDeactivated) {
    const auto assistant = addStaff("Anna Asystent", Role::Assistant);
    ASSERT_TRUE(registry().auth().deactivate(assistant).has_value());

    const StaffViewModel staff;

    ASSERT_EQ(staff.staff()->rowCount(), 2);
    const auto* row = staff.staff()->find(assistant.value);
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->active);
    EXPECT_EQ(staff.currentUserId(), m_coach.value);
}

TEST_F(StaffViewModelTest, WithoutManageStaffTheListStaysEmptyWithoutAnError) {
    const auto assistant = addStaff("Anna Asystent", Role::Assistant);
    ASSERT_TRUE(registry().auth().login(assistant, kPin).has_value());

    StaffViewModel staff;
    staff.reload();

    EXPECT_EQ(staff.staff()->rowCount(), 0);
    EXPECT_TRUE(staff.loadError().isEmpty());
}

TEST_F(StaffViewModelTest, WithoutManageStaffAChangeIsRefused) {
    const auto assistant = addStaff("Anna Asystent", Role::Assistant);
    ASSERT_TRUE(registry().auth().login(assistant, kPin).has_value());
    StaffViewModel staff;
    QString message;
    QObject::connect(&staff, &StaffViewModel::errorOccurred, &staff,
                     [&](const QString& text) { message = text; });

    EXPECT_FALSE(staff.deactivate(m_coach.value));

    EXPECT_FALSE(message.isEmpty());
}

TEST_F(StaffViewModelTest, RoleOptionsOfferEveryRole) {
    const StaffViewModel staff;

    const QVariantList options = staff.roleOptions();

    ASSERT_EQ(options.size(), 4);
    EXPECT_EQ(options.front().toMap().value(QStringLiteral("value")), QStringLiteral("coach"));
    EXPECT_EQ(options.front().toMap().value(QStringLiteral("text")), QStringLiteral("Coach"));
}

TEST_F(StaffViewModelTest, RoleOptionsFollowTheLanguage) {
    const StaffViewModel staff;
    const SignalCount changed(&staff, &StaffViewModel::roleOptionsChanged);

    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &event);

    EXPECT_EQ(changed.count(), 1);
}

TEST_F(StaffViewModelTest, AddPutsTheUserInTheListWithThatPin) {
    StaffViewModel staff;
    const SignalCount changed(&staff, &StaffViewModel::staffChanged);

    EXPECT_TRUE(
        staff.add(QStringLiteral("Piotr Fizjo"), QStringLiteral("physio"), kNewPin, kNewPin));

    EXPECT_EQ(changed.count(), 1);
    ASSERT_EQ(staff.staff()->rowCount(), 2);
    const StaffUser added = staff.staff()->staff().back();
    EXPECT_EQ(added.name, "Piotr Fizjo");
    EXPECT_EQ(added.role, Role::Physio);
    EXPECT_TRUE(added.active);
    const auto check = registry().auth().login(added.id, kNewPin.toStdString());
    ASSERT_TRUE(check.has_value());
    EXPECT_EQ(check->status, PinCheckStatus::Accepted);
}

TEST_F(StaffViewModelTest, AddShowsEveryWrongInputAtOnceAndAddsNothing) {
    StaffViewModel staff;
    const SignalCount changed(&staff, &StaffViewModel::staffChanged);

    EXPECT_FALSE(staff.add(QStringLiteral("  "), QStringLiteral("physio"), QStringLiteral("12"),
                           QStringLiteral("13")));

    EXPECT_FALSE(staff.nameError().isEmpty());
    EXPECT_FALSE(staff.pinError().isEmpty());
    EXPECT_FALSE(staff.pinRepeatError().isEmpty());
    EXPECT_EQ(changed.count(), 0);
    EXPECT_EQ(staff.staff()->rowCount(), 1);
}

TEST_F(StaffViewModelTest, ClearErrorsClearsThemAll) {
    StaffViewModel staff;
    ASSERT_FALSE(staff.add({}, QStringLiteral("physio"), {}, QStringLiteral("1")));

    staff.clearErrors();

    EXPECT_TRUE(staff.nameError().isEmpty());
    EXPECT_TRUE(staff.pinError().isEmpty());
    EXPECT_TRUE(staff.pinRepeatError().isEmpty());
}

TEST_F(StaffViewModelTest, UpdateChangesNameAndRoleAndKeepsTheUserActive) {
    const auto assistant = addStaff("Anna Asystent", Role::Assistant);
    StaffViewModel staff;

    EXPECT_TRUE(
        staff.update(assistant.value, QStringLiteral("Anna Analityk"), QStringLiteral("analyst")));

    const auto* row = staff.staff()->find(assistant.value);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->name, "Anna Analityk");
    EXPECT_EQ(row->role, Role::Analyst);
    EXPECT_TRUE(row->active);
}

TEST_F(StaffViewModelTest, UpdateOfADeactivatedUserKeepsThemDeactivated) {
    const auto assistant = addStaff("Anna Asystent", Role::Assistant);
    ASSERT_TRUE(registry().auth().deactivate(assistant).has_value());
    StaffViewModel staff;

    EXPECT_TRUE(
        staff.update(assistant.value, QStringLiteral("Anna Nowak"), QStringLiteral("assistant")));

    const auto* row = staff.staff()->find(assistant.value);
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->active);
}

TEST_F(StaffViewModelTest, UpdateWithAnEmptyNameShowsWhy) {
    const auto assistant = addStaff("Anna Asystent", Role::Assistant);
    StaffViewModel staff;

    EXPECT_FALSE(staff.update(assistant.value, QStringLiteral(" "), QStringLiteral("assistant")));

    EXPECT_FALSE(staff.nameError().isEmpty());
    EXPECT_EQ(staff.staff()->find(assistant.value)->name, "Anna Asystent");
}

TEST_F(StaffViewModelTest, UpdateOfAnUnknownUserIsReported) {
    StaffViewModel staff;
    QString message;
    QObject::connect(&staff, &StaffViewModel::errorOccurred, &staff,
                     [&](const QString& text) { message = text; });

    EXPECT_FALSE(staff.update(999, QStringLiteral("Nikt"), QStringLiteral("assistant")));

    EXPECT_FALSE(message.isEmpty());
}

TEST_F(StaffViewModelTest, RenamingYourselfKeepsTheSessionUnderTheNewName) {
    StaffViewModel staff;
    const SignalCount changed(&staff, &StaffViewModel::staffChanged);

    EXPECT_TRUE(staff.update(m_coach.value, QStringLiteral("Jan Nowy"), QStringLiteral("coach")));

    EXPECT_EQ(changed.count(), 1);
    ASSERT_TRUE(registry().session().user().has_value());
    EXPECT_EQ(registry().session().user()->name, "Jan Nowy");
}

TEST_F(StaffViewModelTest, ChangingYourOwnRoleEndsTheSessionWithoutAFalseError) {
    addStaff("Druga Trenerka", Role::Coach);
    StaffViewModel staff;
    const SignalCount changed(&staff, &StaffViewModel::staffChanged);
    const SignalCount errors(&staff, &StaffViewModel::errorOccurred);

    EXPECT_TRUE(
        staff.update(m_coach.value, QStringLiteral("Jan Trener"), QStringLiteral("analyst")));

    EXPECT_FALSE(registry().session().user().has_value());
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(errors.count(), 0);
    EXPECT_TRUE(staff.loadError().isEmpty());
}

TEST_F(StaffViewModelTest, TheLastCoachKeepsTheRoleAndTheUserIsTold) {
    StaffViewModel staff;
    QString message;
    QObject::connect(&staff, &StaffViewModel::errorOccurred, &staff,
                     [&](const QString& text) { message = text; });

    EXPECT_FALSE(
        staff.update(m_coach.value, QStringLiteral("Jan Trener"), QStringLiteral("physio")));
    EXPECT_FALSE(message.isEmpty());
    message.clear();
    EXPECT_FALSE(staff.deactivate(m_coach.value));
    EXPECT_FALSE(message.isEmpty());

    EXPECT_TRUE(registry().session().user().has_value());
}

TEST_F(StaffViewModelTest, DeactivateAndUndo) {
    const auto assistant = addStaff("Anna Asystent", Role::Assistant);
    StaffViewModel staff;
    const SignalCount changed(&staff, &StaffViewModel::staffChanged);

    EXPECT_TRUE(staff.deactivate(assistant.value));
    EXPECT_FALSE(staff.staff()->find(assistant.value)->active);
    EXPECT_FALSE(registry().auth().login(assistant, kPin).has_value());

    EXPECT_TRUE(staff.reactivate(assistant.value));
    EXPECT_TRUE(staff.staff()->find(assistant.value)->active);
    EXPECT_EQ(changed.count(), 2);
}

TEST_F(StaffViewModelTest, ResetPinChecksTheInputAndClearsALockout) {
    const auto assistant = addStaff("Anna Asystent", Role::Assistant);
    StaffViewModel staff;
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(registry().auth().login(assistant, "9999").has_value());
    }
    ASSERT_TRUE(registry().auth().login(m_coach, kPin).has_value());

    EXPECT_FALSE(staff.resetPin(assistant.value, kNewPin, QStringLiteral("0000")));
    EXPECT_FALSE(staff.pinRepeatError().isEmpty());

    EXPECT_TRUE(staff.resetPin(assistant.value, kNewPin, kNewPin));
    EXPECT_TRUE(staff.pinRepeatError().isEmpty());
    const auto check = registry().auth().login(assistant, kNewPin.toStdString());
    ASSERT_TRUE(check.has_value());
    EXPECT_EQ(check->status, PinCheckStatus::Accepted);
}

TEST_F(StaffViewModelTest, AnUnreadableListSaysSoWithoutTheDetail) {
    ASSERT_TRUE(m_db->execSql(QStringLiteral("ALTER TABLE staff_user RENAME TO staff_gone"),
                              "break the database")
                    .has_value());

    const StaffViewModel staff;

    EXPECT_FALSE(staff.loadError().isEmpty());
    EXPECT_FALSE(staff.loadError().contains(QLatin1StringView("staff_user")));
}
