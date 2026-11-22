#include "viewmodels/LoadSettingsViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "data/db/DatabaseManager.hpp"
#include "services/LoadService.hpp"
#include "viewmodels/ErrorPresenter.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace fdss::core;
using namespace fdss::tests;
using fdss::viewmodels::ErrorPresenter;
using fdss::viewmodels::LoadSettingsViewModel;

namespace {

// The settings of this test live in a folder of their own, also for the registry, which reads
// them when it is created.
class LoadSettingsViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_settingsDir.isValid());
        m_previousFormat = QSettings::defaultFormat();
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        ViewModelTest::SetUp();
        completeSetup();
    }

    void TearDown() override {
        ViewModelTest::TearDown();
        QSettings::setDefaultFormat(m_previousFormat);
    }

    QTemporaryDir m_settingsDir;
    QSettings::Format m_previousFormat = QSettings::NativeFormat;
};

} // namespace

TEST_F(LoadSettingsViewModelTest, ShowsTheDefaultsAndStoresAChangeWithADecimalComma) {
    LoadSettingsViewModel load;
    EXPECT_TRUE(load.canEdit());
    EXPECT_TRUE(load.isDefault());
    EXPECT_EQ(load.ratioHigh(), QStringLiteral("1.50"));
    EXPECT_EQ(load.ratioElevated(), QStringLiteral("1.30"));
    EXPECT_EQ(load.ratioLow(), QStringLiteral("0.80"));
    EXPECT_EQ(load.monotonyHigh(), QStringLiteral("2.00"));
    const SignalCount changed(&load, &LoadSettingsViewModel::thresholdsChanged);

    ASSERT_TRUE(load.save(QStringLiteral("1,75"), QStringLiteral(" 1.4 "), QStringLiteral("0,7"),
                          QStringLiteral("2.5")));

    EXPECT_EQ(changed.count(), 1);
    EXPECT_FALSE(load.isDefault());
    EXPECT_EQ(load.ratioHigh(), QStringLiteral("1.75"));
    const LoadThresholds& used = registry().load().thresholds();
    EXPECT_DOUBLE_EQ(used.ratioHigh, 1.75);
    EXPECT_DOUBLE_EQ(used.ratioElevated, 1.4);
    EXPECT_DOUBLE_EQ(used.ratioLow, 0.7);
    EXPECT_DOUBLE_EQ(used.monotonyHigh, 2.5);
    EXPECT_EQ(used.acuteDays, LoadThresholds{}.acuteDays);
    EXPECT_EQ(LoadSettingsViewModel::storedThresholds(), used);
}

TEST_F(LoadSettingsViewModelTest, ANewRegistryUsesTheStoredThresholds) {
    LoadThresholds stored;
    {
        LoadSettingsViewModel load;
        ASSERT_TRUE(load.save(QStringLiteral("1.6"), QStringLiteral("1.2"), QStringLiteral("0.9"),
                              QStringLiteral("3")));
        stored = registry().load().thresholds();
    }
    auto opened = fdss::data::DatabaseManager::openInMemory();
    ASSERT_TRUE(opened.has_value());
    const auto next = fdss::viewmodels::ServiceRegistry::create(std::move(*opened));

    EXPECT_EQ(next->load().thresholds(), stored);
}

TEST_F(LoadSettingsViewModelTest, BrokenValuesAreShownOnTheirFieldsAndNothingChanges) {
    LoadSettingsViewModel load;
    const SignalCount errors(&load, &LoadSettingsViewModel::errorsChanged);

    EXPECT_FALSE(load.save(QStringLiteral("abc"), QStringLiteral("1.3"), QStringLiteral("0.8"),
                           QStringLiteral("2")));
    EXPECT_EQ(load.ratioHighError(), ErrorPresenter::invalidInputMessage("ratio_high"));
    EXPECT_TRUE(load.ratioElevatedError().isEmpty());

    // In the wrong order: the elevated ratio is not below the high one.
    EXPECT_FALSE(load.save(QStringLiteral("1.3"), QStringLiteral("1.4"), QStringLiteral("0.8"),
                           QStringLiteral("0.5")));
    EXPECT_TRUE(load.ratioHighError().length() > 0);
    EXPECT_TRUE(load.monotonyHighError().length() > 0);
    EXPECT_TRUE(load.ratioLowError().isEmpty());
    EXPECT_GE(errors.count(), 2);
    EXPECT_TRUE(load.isDefault());
    EXPECT_EQ(LoadSettingsViewModel::storedThresholds(), LoadThresholds{});

    load.clearErrors();
    EXPECT_TRUE(load.ratioHighError().isEmpty());
}

TEST_F(LoadSettingsViewModelTest, RestoringTheDefaultsForgetsTheStoredValues) {
    LoadSettingsViewModel load;
    ASSERT_TRUE(load.save(QStringLiteral("1.6"), QStringLiteral("1.2"), QStringLiteral("0.9"),
                          QStringLiteral("3")));

    ASSERT_TRUE(load.restoreDefaults());

    EXPECT_TRUE(load.isDefault());
    EXPECT_EQ(registry().load().thresholds(), LoadThresholds{});
    EXPECT_FALSE(QSettings().contains(QStringLiteral("load/ratioHigh")));
}

TEST_F(LoadSettingsViewModelTest, OnlyARoleThatManagesSettingsMayChangeThem) {
    const StaffUserId assistant = addStaff("Asystent", Role::Assistant);
    ASSERT_TRUE(registry().auth().login(assistant, kPin).has_value());
    LoadSettingsViewModel load;
    EXPECT_FALSE(load.canEdit());
    const SignalCount refused(&load, &LoadSettingsViewModel::errorOccurred);

    EXPECT_FALSE(load.save(QStringLiteral("1.6"), QStringLiteral("1.2"), QStringLiteral("0.9"),
                           QStringLiteral("3")));
    EXPECT_FALSE(load.restoreDefaults());

    EXPECT_EQ(refused.count(), 2);
    EXPECT_TRUE(load.isDefault());
}

// P19 review: a settings file that cannot be written is reported, and nothing is confirmed or used.
TEST_F(LoadSettingsViewModelTest, AFailedWriteIsReportedAndChangesNothing) {
    LoadSettingsViewModel load;
    ASSERT_TRUE(QFile::setPermissions(m_settingsDir.path(), QFile::ReadOwner | QFile::ExeOwner));
    QFile probe(m_settingsDir.filePath(QStringLiteral("probe")));
    if (probe.open(QIODevice::WriteOnly)) {
        probe.close();
        QFile::setPermissions(m_settingsDir.path(),
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        GTEST_SKIP() << "this user writes into read-only folders (root?)";
    }
    const SignalCount refused(&load, &LoadSettingsViewModel::errorOccurred);
    const SignalCount changed(&load, &LoadSettingsViewModel::thresholdsChanged);

    const bool saved = load.save(QStringLiteral("1.6"), QStringLiteral("1.2"),
                                 QStringLiteral("0.9"), QStringLiteral("3"));
    QFile::setPermissions(m_settingsDir.path(),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    EXPECT_FALSE(saved);
    EXPECT_EQ(refused.count(), 1);
    EXPECT_EQ(changed.count(), 0);
    EXPECT_EQ(registry().load().thresholds(), LoadThresholds{});
    EXPECT_TRUE(load.isDefault());
}

// P19 review: after a failed write this session uses the old thresholds everywhere, also where
// the settings are read again (a registry made now), so Load classifies by them.
TEST_F(LoadSettingsViewModelTest, AfterAFailedWriteLoadClassifiesByTheOldThresholds) {
    LoadSettingsViewModel load;
    ASSERT_TRUE(load.save(QStringLiteral("1.7"), QStringLiteral("1.3"), QStringLiteral("0.8"),
                          QStringLiteral("2")));
    const LoadThresholds old = registry().load().thresholds();
    const QString file = QSettings().fileName();
    const auto writable = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
    ASSERT_TRUE(QFile::setPermissions(file, QFile::ReadOwner));
    ASSERT_TRUE(
        QFile::setPermissions(QFileInfo(file).absolutePath(), QFile::ReadOwner | QFile::ExeOwner));
    QFile probe(file);
    if (probe.open(QIODevice::WriteOnly | QIODevice::Append)) {
        probe.close();
        QFile::setPermissions(QFileInfo(file).absolutePath(), writable);
        QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner);
        GTEST_SKIP() << "this user writes into read-only files (root?)";
    }

    const bool saved = load.save(QStringLiteral("1.6"), QStringLiteral("1.3"),
                                 QStringLiteral("0.8"), QStringLiteral("2"));
    const LoadThresholds inSettings = LoadSettingsViewModel::storedThresholds();
    auto opened = fdss::data::DatabaseManager::openInMemory();
    ASSERT_TRUE(opened.has_value());
    const auto next = fdss::viewmodels::ServiceRegistry::create(std::move(*opened));
    QFile::setPermissions(QFileInfo(file).absolutePath(), writable);
    QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner);

    ASSERT_FALSE(saved);
    EXPECT_EQ(inSettings, old);
    EXPECT_EQ(registry().load().thresholds(), old);
    EXPECT_EQ(next->load().thresholds(), old);
    // 1.65 is High above the refused 1.6, Elevated below the kept 1.7.
    LoadIndicators indicators;
    indicators.rpeDays = 14;
    indicators.daysSinceFirstEntry = 28;
    indicators.acute = 165.0;
    indicators.chronic = 100.0;
    indicators.ratio = 1.65;
    EXPECT_EQ(classifyLoad(indicators, next->load().thresholds()).status, LoadStatus::Elevated);
    EXPECT_EQ(load.ratioHigh(), QStringLiteral("1.70"));
}

TEST_F(LoadSettingsViewModelTest, ThreeDecimalsAreRefusedOnTheirField) {
    LoadSettingsViewModel load;
    EXPECT_FALSE(load.save(QStringLiteral("1,501"), QStringLiteral("1.3"), QStringLiteral("0.8"),
                           QStringLiteral("2")));
    EXPECT_EQ(load.ratioHighError(), ErrorPresenter::invalidInputMessage("ratio_high"));
    EXPECT_TRUE(load.isDefault());
}
