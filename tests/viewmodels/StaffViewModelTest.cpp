#include "viewmodels/StaffViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QStringList>
#include <QVariantMap>

#include <gtest/gtest.h>

#include <functional>
#include <vector>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::services::PinCheckStatus;
using fdss::viewmodels::ErrorPresenter;
using fdss::viewmodels::StaffListModel;
using fdss::viewmodels::StaffViewModel;

namespace {

const auto kNewPin = QStringLiteral("5678");
constexpr auto kRefusedHere = "refused by the viewmodel";

// What gets logged while it lives. The service refuses with the same error code and text for the
// user, so only the log tells that it was never called.
class LogCapture {
public:
    LogCapture() : m_previous(qInstallMessageHandler(capture)) { s_messages.clear(); }
    ~LogCapture() { qInstallMessageHandler(m_previous); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] static const QStringList& messages() { return s_messages; }

private:
    static void capture(QtMsgType, const QMessageLogContext&, const QString& message) {
        s_messages.append(message);
    }

    static inline QStringList s_messages;
    QtMessageHandler m_previous;
};

struct StaffChange {
    const char* name;
    std::function<bool(StaffViewModel&, qint64)> run;
};

std::vector<StaffChange> everyStaffChange() {
    return {
        {"add",
         [](StaffViewModel& staff, qint64) {
             return staff.add(QStringLiteral("Nowa Osoba"), QStringLiteral("physio"), kNewPin,
                              kNewPin);
         }},
        {"update",
         [](StaffViewModel& staff, qint64 id) {
             return staff.update(id, QStringLiteral("Inne Nazwisko"), QStringLiteral("physio"));
         }},
        {"resetPin",
         [](StaffViewModel& staff, qint64 id) { return staff.resetPin(id, kNewPin, kNewPin); }},
        {"deactivate", [](StaffViewModel& staff, qint64 id) { return staff.deactivate(id); }},
        {"reactivate", [](StaffViewModel& staff, qint64 id) { return staff.reactivate(id); }},
    };
}

class StaffViewModelTest : public ViewModelTest {
protected:
    // A Coach, logged in.
    void SetUp() override {
        ViewModelTest::SetUp();
        m_coach = completeSetup().id;
    }

    static void expectEveryChangeRefused(StaffViewModel& staff, StaffUserId target) {
        const QString refusal = ErrorPresenter::message(Error{ErrorCode::PermissionDenied, {}, {}});
        for (const StaffChange& change : everyStaffChange()) {
            SCOPED_TRACE(change.name);
            QStringList errors;
            const QMetaObject::Connection connection =
                QObject::connect(&staff, &StaffViewModel::errorOccurred, &staff,
                                 [&](const QString& text) { errors.append(text); });
            const SignalCount changed(&staff, &StaffViewModel::staffChanged);
            const LogCapture log;

            EXPECT_FALSE(change.run(staff, target.value));

            QObject::disconnect(connection);
            EXPECT_EQ(errors, QStringList{refusal});
            EXPECT_EQ(changed.count(), 0);
            ASSERT_EQ(LogCapture::messages().size(), 1) << "the service logs a refusal of its own";
            EXPECT_TRUE(LogCapture::messages().front().contains(QLatin1StringView(kRefusedHere)));
        }
    }

    // Logs the Coach in to read the staff, and the target to prove that the PIN stayed.
    void expectNothingChanged(const std::vector<StaffUser>& before, StaffUserId target) const {
        ASSERT_TRUE(registry().auth().login(m_coach, kPin).has_value());
        const auto after = registry().auth().listAllStaff();
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(*after, before);
        const auto check = registry().auth().login(target, kPin);
        ASSERT_TRUE(check.has_value());
        EXPECT_EQ(check->status, PinCheckStatus::Accepted);
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

TEST_F(StaffViewModelTest, EveryChangeIsRefusedHereForARoleWithoutManageStaff) {
    const StaffUserId target = addStaff("Anna Cel", Role::Assistant);
    const StaffUserId assistant = addStaff("Adam Asystent", Role::Assistant);
    const StaffUserId physio = addStaff("Piotr Fizjo", Role::Physio);
    const StaffUserId analyst = addStaff("Ewa Analityk", Role::Analyst);
    const auto before = registry().auth().listAllStaff();
    ASSERT_TRUE(before.has_value());

    for (const StaffUserId user : {assistant, physio, analyst}) {
        ASSERT_TRUE(registry().auth().login(user, kPin).has_value());
        StaffViewModel staff;
        expectEveryChangeRefused(staff, target);
    }

    expectNothingChanged(before.value(), target);
}

TEST_F(StaffViewModelTest, EveryChangeIsRefusedHereAfterALogout) {
    const StaffUserId target = addStaff("Anna Cel", Role::Assistant);
    const auto before = registry().auth().listAllStaff();
    ASSERT_TRUE(before.has_value());
    StaffViewModel staff; // built while the Coach was logged in, with the list read
    ASSERT_NE(staff.staff()->find(target.value), nullptr);

    registry().auth().logout();
    expectEveryChangeRefused(staff, target);

    expectNothingChanged(before.value(), target);
}

TEST_F(StaffViewModelTest, TheRefusalComesBeforeTheInputIsChecked) {
    registry().auth().logout();
    StaffViewModel staff;

    EXPECT_FALSE(staff.add({}, QStringLiteral("physio"), {}, QStringLiteral("1")));
    EXPECT_FALSE(staff.update(m_coach.value, {}, QStringLiteral("coach")));
    EXPECT_FALSE(staff.resetPin(m_coach.value, {}, QStringLiteral("1")));

    EXPECT_TRUE(staff.nameError().isEmpty());
    EXPECT_TRUE(staff.pinError().isEmpty());
    EXPECT_TRUE(staff.pinRepeatError().isEmpty());
}

TEST_F(StaffViewModelTest, RoleOptionsOfferEveryRole) {
    const StaffViewModel staff;

    const QVariantList options = staff.roleOptions();

    EXPECT_TRUE(staff.loadError().isEmpty());
    ASSERT_EQ(options.size(), 4);
    EXPECT_EQ(options.front().toMap().value(QStringLiteral("value")), QStringLiteral("coach"));
    EXPECT_EQ(options.front().toMap().value(QStringLiteral("text")), QStringLiteral("Coach"));
}

TEST_F(StaffViewModelTest, RoleOptionsFollowTheLanguage) {
    const StaffViewModel staff;
    const SignalCount changed(&staff, &StaffViewModel::roleOptionsChanged);
    const SignalCount loadError(&staff, &StaffViewModel::loadErrorChanged);

    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &event);

    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(loadError.count(), 1);
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
