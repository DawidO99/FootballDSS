#include "viewmodels/NavigationModel.hpp"

#include <gtest/gtest.h>

using fdss::viewmodels::NavigationModel;

namespace {

const QVariantMap kSession{{QStringLiteral("sessionId"), 5}};
const QVariantMap kOtherSession{{QStringLiteral("sessionId"), 6}};

// Counts currentIndexChanged, the notify signal of currentIndex, currentKey and currentArgs.
class NavigationModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        QObject::connect(&m_model, &NavigationModel::currentIndexChanged, &m_model,
                         [this] { ++m_notifications; });
    }

    NavigationModel m_model;
    int m_notifications = 0;
};

} // namespace

TEST_F(NavigationModelTest, StartsOnTheFirstSectionWithoutArguments) {
    EXPECT_EQ(m_model.currentKey(), QStringLiteral("dashboard"));
    EXPECT_TRUE(m_model.currentArgs().isEmpty());
}

TEST_F(NavigationModelTest, ActivateSelectsTheSectionAndKeepsTheArguments) {
    m_model.activate(QStringLiteral("training"), kSession);

    EXPECT_EQ(m_model.currentKey(), QStringLiteral("training"));
    EXPECT_EQ(m_model.currentArgs(), kSession);
    EXPECT_EQ(m_notifications, 1);
}

TEST_F(NavigationModelTest, ActivateWithoutArgumentsClearsThem) {
    m_model.activate(QStringLiteral("training"), kSession);

    m_model.activate(QStringLiteral("squad"));

    EXPECT_EQ(m_model.currentKey(), QStringLiteral("squad"));
    EXPECT_TRUE(m_model.currentArgs().isEmpty());
}

TEST_F(NavigationModelTest, NewArgumentsForTheCurrentSectionNotify) {
    m_model.activate(QStringLiteral("training"), kSession);
    const int index = m_model.currentIndex();

    m_model.activate(QStringLiteral("training"), kOtherSession);

    EXPECT_EQ(m_model.currentIndex(), index);
    EXPECT_EQ(m_model.currentArgs(), kOtherSession);
    EXPECT_EQ(m_notifications, 2);
}

TEST_F(NavigationModelTest, SameSectionAndArgumentsDoNotNotifyAgain) {
    m_model.activate(QStringLiteral("training"), kSession);

    m_model.activate(QStringLiteral("training"), kSession);

    EXPECT_EQ(m_notifications, 1);
}

TEST_F(NavigationModelTest, MovingThroughTheSidebarClearsTheArguments) {
    m_model.activate(QStringLiteral("training"), kSession);

    m_model.setCurrentIndex(0);

    EXPECT_EQ(m_model.currentKey(), QStringLiteral("dashboard"));
    EXPECT_TRUE(m_model.currentArgs().isEmpty());
}

// The sidebar writes its index back while reacting to the change; that must not drop the arguments.
TEST_F(NavigationModelTest, WritingTheSameIndexBackKeepsTheArguments) {
    m_model.activate(QStringLiteral("training"), kSession);

    m_model.setCurrentIndex(m_model.currentIndex());

    EXPECT_EQ(m_model.currentArgs(), kSession);
    EXPECT_EQ(m_notifications, 1);
}

TEST_F(NavigationModelTest, GuardStopsTheSidebarAndActivate) {
    m_model.activate(QStringLiteral("training"), kSession);
    const int training = m_model.currentIndex();
    int requests = 0;
    QObject::connect(&m_model, &NavigationModel::leaveRequested, &m_model, [&] { ++requests; });
    m_model.setLeaveGuard(true);

    m_model.setCurrentIndex(0);
    EXPECT_EQ(requests, 1);
    m_model.activate(QStringLiteral("squad"), kOtherSession);
    EXPECT_EQ(requests, 2);

    EXPECT_EQ(m_model.currentIndex(), training);
    EXPECT_EQ(m_model.currentArgs(), kSession);
    EXPECT_EQ(m_notifications, 1);
}

TEST_F(NavigationModelTest, ResumeGoesToTheLastTargetAndEndsTheGuard) {
    m_model.activate(QStringLiteral("training"), kSession);
    m_model.setLeaveGuard(true);
    m_model.activate(QStringLiteral("squad"), kOtherSession);

    m_model.resumeLeave();

    EXPECT_FALSE(m_model.leaveGuard());
    EXPECT_EQ(m_model.currentKey(), QStringLiteral("squad"));
    EXPECT_EQ(m_model.currentArgs(), kOtherSession);
}

// A save or a discard turns the guard off before the view answers the question.
TEST_F(NavigationModelTest, TurningTheGuardOffKeepsTheTargetForResume) {
    m_model.activate(QStringLiteral("training"));
    m_model.setLeaveGuard(true);
    m_model.setCurrentIndex(0);

    m_model.setLeaveGuard(false);
    EXPECT_EQ(m_model.currentKey(), QStringLiteral("training"));

    m_model.resumeLeave();
    EXPECT_EQ(m_model.currentKey(), QStringLiteral("dashboard"));
}

TEST_F(NavigationModelTest, CancelStaysAndNotifiesTheSidebar) {
    m_model.activate(QStringLiteral("training"), kSession);
    m_model.setLeaveGuard(true);
    m_model.setCurrentIndex(0);
    const int before = m_notifications;

    m_model.cancelLeave();

    EXPECT_TRUE(m_model.leaveGuard());
    EXPECT_EQ(m_model.currentKey(), QStringLiteral("training"));
    EXPECT_EQ(m_model.currentArgs(), kSession);
    EXPECT_EQ(m_notifications, before + 1);

    // Nothing left to go to.
    m_model.resumeLeave();
    EXPECT_EQ(m_model.currentKey(), QStringLiteral("training"));
}

TEST_F(NavigationModelTest, GuardAsksNothingWithoutAMove) {
    m_model.activate(QStringLiteral("training"), kSession);
    int requests = 0;
    QObject::connect(&m_model, &NavigationModel::leaveRequested, &m_model, [&] { ++requests; });
    m_model.setLeaveGuard(true);

    m_model.setCurrentIndex(m_model.currentIndex());
    m_model.activate(QStringLiteral("training"), kSession);

    EXPECT_EQ(requests, 0);
}

TEST_F(NavigationModelTest, UnknownSectionChangesNothing) {
    m_model.activate(QStringLiteral("training"), kSession);

    m_model.activate(QStringLiteral("nope"), kOtherSession);

    EXPECT_EQ(m_model.currentKey(), QStringLiteral("training"));
    EXPECT_EQ(m_model.currentArgs(), kSession);
}

TEST_F(NavigationModelTest, TheMatchReportIsASectionWithoutASidebarItem) {
    const int last = m_model.rowCount() - 1;
    EXPECT_EQ(m_model.sidebarCount(), last);
    EXPECT_EQ(m_model.data(m_model.index(last), NavigationModel::KeyRole),
              QStringLiteral("matchreport"));
    EXPECT_FALSE(m_model.data(m_model.index(last), NavigationModel::InSidebarRole).toBool());
    EXPECT_TRUE(m_model.data(m_model.index(0), NavigationModel::InSidebarRole).toBool());

    const QVariantMap match{{QStringLiteral("matchId"), 3}};
    m_model.activate(QStringLiteral("matchreport"), match);
    EXPECT_EQ(m_model.currentKey(), QStringLiteral("matchreport"));
    EXPECT_EQ(m_model.currentArgs(), match);
}
