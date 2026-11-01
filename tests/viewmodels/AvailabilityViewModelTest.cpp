#include "viewmodels/AvailabilityViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/Player.hpp"
#include "services/Session.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::AvailabilityListModel;
using fdss::viewmodels::AvailabilityViewModel;
using fdss::viewmodels::ErrorPresenter;
using fdss::viewmodels::IsoDates;

namespace {

const auto kInjury = QStringLiteral("injury");
const auto kAbsence = QStringLiteral("absence");
const auto kLastMonth = QStringLiteral("2026-10-01");
const auto kToday = QStringLiteral("2026-11-01");
const auto kNextMonth = QStringLiteral("2026-12-01");
const auto kSecret = QStringLiteral("Naderwany mięsień");

class AvailabilityViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        PlayerDraft draft;
        draft.firstName = "Jan";
        draft.lastName = "Kowalski";
        const auto id =
            registry().squad().addPlayer(registry().setup().clubContext()->team.id, draft);
        ASSERT_TRUE(id.has_value());
        m_player = *id;
        m_assistant = addStaff("Asystent", Role::Assistant);
        m_physio = addStaff("Fizjo", Role::Physio);
        m_analyst = addStaff("Analityk", Role::Analyst);
        m_coach = registry().session().user()->id;
    }

    void logInAs(StaffUserId id) const {
        registry().auth().logout();
        ASSERT_TRUE(registry().auth().login(id, kPin).has_value());
    }

    // An injury with a description, added by the Coach, who stays logged in.
    qint64 addSecretInjury(AvailabilityViewModel& availability) const {
        availability.setPlayerId(m_player.value);
        EXPECT_TRUE(availability.add(kInjury, kLastMonth, {}, kSecret));
        return firstRow(availability).value(QStringLiteral("entryId")).toLongLong();
    }

    [[nodiscard]] static QVariantMap firstRow(const AvailabilityViewModel& availability) {
        return availability.entries()->rowAt(0);
    }

    PlayerId m_player;
    StaffUserId m_coach;
    StaffUserId m_assistant;
    StaffUserId m_physio;
    StaffUserId m_analyst;
};

TEST_F(AvailabilityViewModelTest, RightsFollowTheRole) {
    struct Expected {
        StaffUserId user;
        bool canEdit;
        bool canViewDetails;
    };
    for (const Expected& expected :
         {Expected{m_coach, true, true}, Expected{m_assistant, true, false},
          Expected{m_physio, true, true}, Expected{m_analyst, false, false}}) {
        logInAs(expected.user);
        AvailabilityViewModel availability;
        EXPECT_EQ(availability.canEdit(), expected.canEdit);
        EXPECT_EQ(availability.canViewDetails(), expected.canViewDetails);
    }
}

TEST_F(AvailabilityViewModelTest, ListShowsEveryValueNewestFirst) {
    AvailabilityViewModel availability;
    availability.setPlayerId(m_player.value);
    SignalCount changed(&availability, &AvailabilityViewModel::availabilityChanged);
    ASSERT_TRUE(availability.add(kAbsence, kLastMonth, kToday, {}));
    ASSERT_TRUE(availability.add(kInjury, kToday, kNextMonth, kSecret));
    EXPECT_EQ(changed.count(), 2);

    ASSERT_EQ(availability.entries()->rowCount(), 2);
    const QVariantMap row = firstRow(availability);
    EXPECT_EQ(row.value(QStringLiteral("type")).toString(), kInjury);
    EXPECT_FALSE(row.value(QStringLiteral("typeName")).toString().isEmpty());
    EXPECT_EQ(row.value(QStringLiteral("fromDate")).toString(), kToday);
    EXPECT_EQ(row.value(QStringLiteral("expectedTo")).toString(), kNextMonth);
    EXPECT_TRUE(row.value(QStringLiteral("actualTo")).toString().isEmpty());
    EXPECT_EQ(row.value(QStringLiteral("fromText")).toString(), QStringLiteral("01/11/2026"));
    EXPECT_EQ(row.value(QStringLiteral("description")).toString(), kSecret);
    EXPECT_TRUE(row.value(QStringLiteral("open")).toBool());
}

TEST_F(AvailabilityViewModelTest, PhysioSeesTheDescriptionAssistantAndAnalystDoNot) {
    AvailabilityViewModel coachView;
    addSecretInjury(coachView);

    logInAs(m_physio);
    {
        AvailabilityViewModel availability;
        availability.setPlayerId(m_player.value);
        EXPECT_EQ(firstRow(availability).value(QStringLiteral("description")).toString(), kSecret);
    }
    for (const StaffUserId user : {m_assistant, m_analyst}) {
        logInAs(user);
        AvailabilityViewModel availability;
        availability.setPlayerId(m_player.value);
        const QVariantMap row = firstRow(availability);
        EXPECT_EQ(row.value(QStringLiteral("type")).toString(), kInjury);
        EXPECT_EQ(row.value(QStringLiteral("fromDate")).toString(), kLastMonth);
        EXPECT_TRUE(row.value(QStringLiteral("description")).toString().isEmpty());
    }
}

TEST_F(AvailabilityViewModelTest, AssistantSavingAnEntryKeepsTheDescription) {
    AvailabilityViewModel coachView;
    const qint64 id = addSecretInjury(coachView);

    logInAs(m_assistant);
    {
        AvailabilityViewModel availability;
        availability.setPlayerId(m_player.value);
        // Whatever the view passes: an Assistant has no way to the text.
        ASSERT_TRUE(availability.update(id, kInjury, kLastMonth, kNextMonth, {}, QString{}));
        EXPECT_EQ(firstRow(availability).value(QStringLiteral("expectedTo")).toString(),
                  kNextMonth);
    }

    logInAs(m_coach);
    coachView.reload();
    EXPECT_EQ(firstRow(coachView).value(QStringLiteral("description")).toString(), kSecret);
}

TEST_F(AvailabilityViewModelTest, AnEmptyTextClearsTheDescriptionForARoleThatSeesIt) {
    AvailabilityViewModel availability;
    const qint64 id = addSecretInjury(availability);
    ASSERT_TRUE(availability.update(id, kInjury, kLastMonth, {}, {}, QString{}));
    EXPECT_TRUE(firstRow(availability).value(QStringLiteral("description")).toString().isEmpty());
}

TEST_F(AvailabilityViewModelTest, AnalystIsRefusedBeforeTheService) {
    AvailabilityViewModel coachView;
    const qint64 id = addSecretInjury(coachView);
    logInAs(m_analyst);

    AvailabilityViewModel availability;
    availability.setPlayerId(m_player.value);
    SignalCount errors(&availability, &AvailabilityViewModel::errorOccurred);
    EXPECT_FALSE(availability.add(kInjury, kToday, {}, {}));
    EXPECT_FALSE(availability.update(id, kInjury, kToday, {}, {}, {}));
    EXPECT_FALSE(availability.markReturned(id, kToday));
    EXPECT_EQ(errors.count(), 3);
    EXPECT_EQ(availability.entries()->rowCount(), 1);
}

TEST_F(AvailabilityViewModelTest, BrokenDatesShowTheErrorOnTheField) {
    AvailabilityViewModel availability;
    availability.setPlayerId(m_player.value);

    EXPECT_FALSE(availability.add(kInjury, {}, {}, {}));
    EXPECT_EQ(availability.fromDateError(), ErrorPresenter::invalidInputMessage("from_date"));

    EXPECT_FALSE(availability.add(kInjury, kToday, kLastMonth, {}));
    EXPECT_TRUE(availability.fromDateError().isEmpty());
    EXPECT_EQ(availability.expectedToError(), ErrorPresenter::invalidInputMessage("expected_to"));

    EXPECT_FALSE(availability.add(kInjury, kToday, QStringLiteral("nonsense"), {}));
    EXPECT_FALSE(availability.expectedToError().isEmpty());

    availability.clearErrors();
    EXPECT_TRUE(availability.expectedToError().isEmpty());
    EXPECT_EQ(availability.entries()->rowCount(), 0);
}

TEST_F(AvailabilityViewModelTest, MarkAsReturnedClosesTheEntry) {
    AvailabilityViewModel availability;
    const qint64 id = addSecretInjury(availability);
    SignalCount changed(&availability, &AvailabilityViewModel::availabilityChanged);

    ASSERT_TRUE(availability.markReturned(id, kToday));

    EXPECT_EQ(changed.count(), 1);
    const QVariantMap row = firstRow(availability);
    EXPECT_EQ(row.value(QStringLiteral("actualTo")).toString(), kToday);
    EXPECT_FALSE(row.value(QStringLiteral("open")).toBool());
    EXPECT_EQ(row.value(QStringLiteral("description")).toString(), kSecret);
}

TEST_F(AvailabilityViewModelTest, MarkAsReturnedBeforeTheStartShowsTheErrorOnTheField) {
    AvailabilityViewModel availability;
    const qint64 id = addSecretInjury(availability);

    EXPECT_FALSE(availability.markReturned(id, QStringLiteral("2026-09-01")));
    EXPECT_EQ(availability.actualToError(), ErrorPresenter::invalidInputMessage("actual_to"));
    EXPECT_TRUE(firstRow(availability).value(QStringLiteral("open")).toBool());
}

TEST_F(AvailabilityViewModelTest, TheReturnOfferedIsTodayButNotBeforeTheStart) {
    AvailabilityViewModel availability;
    const qint64 started = addSecretInjury(availability);
    EXPECT_EQ(availability.defaultReturnDate(started), kToday);
    EXPECT_EQ(availability.today(), kToday);

    ASSERT_TRUE(availability.add(kAbsence, kNextMonth, {}, {}));
    const qint64 planned = firstRow(availability).value(QStringLiteral("entryId")).toLongLong();
    EXPECT_EQ(availability.defaultReturnDate(planned), kNextMonth);
}

TEST_F(AvailabilityViewModelTest, AnEditCanCorrectAReturnOrTakeItBack) {
    AvailabilityViewModel availability;
    const qint64 id = addSecretInjury(availability);
    ASSERT_TRUE(availability.markReturned(id, kToday));

    ASSERT_TRUE(availability.update(id, kInjury, kLastMonth, {}, {}, kSecret));
    EXPECT_TRUE(firstRow(availability).value(QStringLiteral("open")).toBool());
}

TEST(IsoDatesTest, ConvertsBothWays) {
    const Date date{std::chrono::year{2026}, std::chrono::February, std::chrono::day{3}};
    EXPECT_EQ(IsoDates::toIso(date), QStringLiteral("2026-02-03"));
    EXPECT_EQ(IsoDates::parse(QStringLiteral("2026-02-03")), date);
    EXPECT_TRUE(IsoDates::toIso(std::optional<Date>{}).isEmpty());
    EXPECT_TRUE(IsoDates::displayText(std::nullopt).isEmpty());
    EXPECT_EQ(IsoDates::displayText(date), QStringLiteral("03/02/2026"));
}

TEST(IsoDatesTest, RefusesWhatIsNotARealDate) {
    for (const char* text : {"", "2026-2-3", "2026-02-30", "03.02.2026", "nonsense"}) {
        EXPECT_FALSE(IsoDates::parse(QString::fromLatin1(text)).has_value()) << text;
    }
}

} // namespace
