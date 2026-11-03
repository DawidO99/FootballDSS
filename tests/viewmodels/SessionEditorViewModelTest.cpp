#include "viewmodels/SessionEditorViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "core/domain/TrainingSession.hpp"
#include "core/domain/Validation.hpp"
#include "services/CalendarService.hpp"
#include "viewmodels/ErrorPresenter.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::ErrorPresenter;
using fdss::viewmodels::SessionEditorViewModel;

namespace {

constexpr auto kDay = "2026-11-07";

class SessionEditorViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        if (HasFatalFailure()) {
            return;
        }
        completeSetup();
        m_team = registry().setup().clubContext()->team.id;
    }

    TrainingSessionId addSession() {
        TrainingSessionDraft draft;
        draft.date = Date{std::chrono::year{2026}, std::chrono::November, std::chrono::day{7}};
        draft.startTime = std::chrono::hours{17};
        draft.durationMin = 60;
        draft.type = SessionType::Gym;
        draft.note = "Siłownia";
        const auto id = registry().calendar().addSession(m_team, draft);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(TrainingSessionId{});
    }

    TeamId m_team;
};

TEST_F(SessionEditorViewModelTest, ANewSessionStartsWithTheDefaults) {
    SessionEditorViewModel editor;
    editor.startAdding(QString::fromUtf8(kDay));

    EXPECT_TRUE(editor.adding());
    EXPECT_EQ(editor.sessionId(), 0);
    EXPECT_EQ(editor.date(), QString::fromUtf8(kDay));
    EXPECT_TRUE(editor.startTime().isEmpty());
    EXPECT_EQ(editor.durationMin().toInt(), 90);
    EXPECT_EQ(editor.type(), QStringLiteral("training"));
    EXPECT_FALSE(editor.dirty());
    EXPECT_EQ(editor.minDuration(), kMinSessionDuration);
    EXPECT_EQ(editor.maxDuration(), kMaxSessionDuration);
}

TEST_F(SessionEditorViewModelTest, AddsASessionWithEveryField) {
    SessionEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding(QString::fromUtf8(kDay));
    editor.setStartTime(QStringLiteral("09:15"));
    editor.setDurationMin(45);
    editor.setType(QStringLiteral("recovery"));
    editor.setNote(QStringLiteral("Po meczu"));
    SignalCount saved(&editor, &SessionEditorViewModel::saved);

    ASSERT_TRUE(editor.save());
    EXPECT_EQ(saved.count(), 1);

    const auto items = registry().calendar().listItems(m_team);
    ASSERT_TRUE(items.has_value());
    ASSERT_EQ(items->sessions.size(), 1U);
    const TrainingSession& session = items->sessions.front();
    EXPECT_EQ(session.startTime, std::chrono::hours{9} + std::chrono::minutes{15});
    EXPECT_EQ(session.durationMin, 45);
    EXPECT_EQ(session.type, SessionType::Recovery);
    EXPECT_EQ(session.note, "Po meczu");
}

TEST_F(SessionEditorViewModelTest, TheDurationLimitsAreFieldErrors) {
    SessionEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding(QString::fromUtf8(kDay));
    const QString expected =
        ErrorPresenter::message(Error{ErrorCode::InvalidInput, {}, "duration_min"});

    for (const int minutes : {kMinSessionDuration - 1, kMaxSessionDuration + 1}) {
        SCOPED_TRACE(minutes);
        editor.setDurationMin(minutes);
        EXPECT_FALSE(editor.save());
        EXPECT_EQ(editor.durationError(), expected);
    }
    editor.setDurationMin(QVariant{});
    EXPECT_TRUE(editor.durationError().isEmpty());
    EXPECT_FALSE(editor.save());
    EXPECT_EQ(editor.durationError(), expected);

    for (const int minutes : {kMinSessionDuration, kMaxSessionDuration}) {
        SCOPED_TRACE(minutes);
        editor.setDurationMin(minutes);
        EXPECT_TRUE(editor.save());
    }
}

TEST_F(SessionEditorViewModelTest, ADateIsNeededAndATimeMustBeReal) {
    SessionEditorViewModel editor;
    editor.setTeamId(m_team.value);
    editor.startAdding(QString{});
    editor.setStartTime(QStringLiteral("25:00"));
    editor.setDurationMin(5);

    EXPECT_FALSE(editor.save());

    EXPECT_FALSE(editor.dateError().isEmpty());
    EXPECT_FALSE(editor.startTimeError().isEmpty());
    EXPECT_FALSE(editor.durationError().isEmpty());
}

TEST_F(SessionEditorViewModelTest, EditingLoadsAndSavesTheSession) {
    const TrainingSessionId session = addSession();
    SessionEditorViewModel editor;

    ASSERT_TRUE(editor.startEditing(session.value));
    EXPECT_FALSE(editor.adding());
    EXPECT_EQ(editor.sessionId(), session.value);
    EXPECT_EQ(editor.date(), QString::fromUtf8(kDay));
    EXPECT_EQ(editor.startTime(), QStringLiteral("17:00"));
    EXPECT_EQ(editor.durationMin().toInt(), 60);
    EXPECT_EQ(editor.type(), QStringLiteral("gym"));
    EXPECT_EQ(editor.note(), QStringLiteral("Siłownia"));

    editor.setStartTime(QString{});
    editor.setNote(QString{});
    ASSERT_TRUE(editor.save());

    const auto stored = registry().calendar().session(session);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->startTime.has_value());
    EXPECT_FALSE(stored->note.has_value());
}

TEST_F(SessionEditorViewModelTest, DirtyFollowsTheDifferenceFromWhatWasLoaded) {
    const TrainingSessionId session = addSession();
    SessionEditorViewModel editor;
    ASSERT_TRUE(editor.startEditing(session.value));
    EXPECT_FALSE(editor.dirty());
    SignalCount changed(&editor, &SessionEditorViewModel::dirtyChanged);

    editor.setDurationMin(75);
    EXPECT_TRUE(editor.dirty());
    editor.setDurationMin(60);
    EXPECT_FALSE(editor.dirty());
    editor.setNote(QStringLiteral("Inna"));
    EXPECT_TRUE(editor.dirty());
    EXPECT_EQ(changed.count(), 3);

    ASSERT_TRUE(editor.save());
    EXPECT_FALSE(editor.dirty());
}

TEST_F(SessionEditorViewModelTest, AMissingSessionIsReported) {
    SessionEditorViewModel editor;
    SignalCount errors(&editor, &SessionEditorViewModel::errorOccurred);
    EXPECT_FALSE(editor.startEditing(999));
    EXPECT_EQ(errors.count(), 1);
}

TEST_F(SessionEditorViewModelTest, ARoleWithoutEditCalendarSeesAndCannotChange) {
    const TrainingSessionId session = addSession();
    const StaffUserId physio = addStaff("Fizjo", Role::Physio);
    const auto login = registry().auth().login(physio, kPin);
    ASSERT_TRUE(login.has_value());
    SessionEditorViewModel editor;
    SignalCount errors(&editor, &SessionEditorViewModel::errorOccurred);

    ASSERT_TRUE(editor.startEditing(session.value));
    EXPECT_TRUE(editor.readOnly());
    EXPECT_FALSE(editor.save());
    EXPECT_EQ(errors.count(), 1);
}

} // namespace
