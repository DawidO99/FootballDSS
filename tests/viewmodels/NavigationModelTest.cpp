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

TEST_F(NavigationModelTest, UnknownSectionChangesNothing) {
    m_model.activate(QStringLiteral("training"), kSession);

    m_model.activate(QStringLiteral("nope"), kOtherSession);

    EXPECT_EQ(m_model.currentKey(), QStringLiteral("training"));
    EXPECT_EQ(m_model.currentArgs(), kSession);
}
