#include "viewmodels/ImportWizardViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/RowsModel.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace fdss;
using namespace fdss::tests;
using fdss::viewmodels::ImportWizardViewModel;

namespace {

QString fixture(const char* name) {
    return QStringLiteral(FDSS_IMPORT_FIXTURES_DIR "/%1").arg(QLatin1StringView(name));
}

bool waitIdle(const ImportWizardViewModel& wizard) {
    QElapsedTimer timer;
    timer.start();
    // The result comes in through the event loop even when the work is done at once.
    QCoreApplication::processEvents();
    while (wizard.busy() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return !wizard.busy();
}

class ImportWizardViewModelTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        completeSetup();
        m_team = scalarTeam();
        ASSERT_NE(m_team, 0);
    }

    [[nodiscard]] qint64 scalarTeam() const {
        QSqlQuery query(m_db->database());
        query.exec(QStringLiteral("SELECT id FROM team"));
        return query.next() ? query.value(0).toLongLong() : 0;
    }

    [[nodiscard]] int playerCount() const {
        QSqlQuery query(m_db->database());
        query.exec(QStringLiteral("SELECT count(*) FROM player WHERE archived_at IS NULL"));
        return query.next() ? query.value(0).toInt() : -1;
    }

    // Type, file, preview: the wizard on the preview step of `name`.
    void open(ImportWizardViewModel& wizard, const char* name) {
        wizard.setTeamId(m_team);
        wizard.chooseType(QStringLiteral("squad"));
        ASSERT_EQ(wizard.step(), QStringLiteral("file"));
        wizard.openPath(fixture(name));
        ASSERT_TRUE(waitIdle(wizard));
    }

    void validate(ImportWizardViewModel& wizard) {
        wizard.validate();
        ASSERT_TRUE(waitIdle(wizard));
        ASSERT_EQ(wizard.step(), QStringLiteral("report"));
    }

    qint64 m_team = 0;
};

TEST_F(ImportWizardViewModelTest, WholeWizardWithThePolishExcelFile) {
    ImportWizardViewModel wizard;
    SignalCount squadChanged(&wizard, &ImportWizardViewModel::squadChanged);
    EXPECT_TRUE(wizard.canImport());
    EXPECT_EQ(wizard.step(), QStringLiteral("type"));
    ASSERT_NO_FATAL_FAILURE(open(wizard, "polish-excel-cp1250.csv"));
    EXPECT_EQ(wizard.step(), QStringLiteral("preview"));
    EXPECT_TRUE(wizard.errorText().isEmpty());
    EXPECT_EQ(wizard.rowCount(), 20);
    EXPECT_EQ(wizard.previewRows().size(), 20);
    EXPECT_EQ(wizard.previewColumns().size(), 5); // the row number and four columns
    EXPECT_TRUE(wizard.sheetOptions().isEmpty());
    EXPECT_TRUE(wizard.duplicateWarning().isEmpty());

    wizard.goToMapping();
    EXPECT_EQ(wizard.step(), QStringLiteral("mapping"));
    ASSERT_EQ(wizard.mapping()->rowCount(), 4);
    EXPECT_EQ(wizard.mapping()->value(0, "field").toString(), QStringLiteral("last_name"));
    EXPECT_EQ(wizard.mapping()->value(1, "field").toString(), QStringLiteral("first_name"));
    EXPECT_EQ(wizard.mapping()->value(2, "field").toString(), QStringLiteral("shirt_number"));
    EXPECT_EQ(wizard.mapping()->value(3, "field").toString(), QStringLiteral("primary_position"));
    EXPECT_TRUE(wizard.mapping()->value(0, "autoMatched").toBool());
    EXPECT_FALSE(wizard.defaultPositionRequired());
    EXPECT_TRUE(wizard.mappingErrors().isEmpty());

    ASSERT_NO_FATAL_FAILURE(validate(wizard));
    EXPECT_EQ(wizard.okCount(), 20);
    EXPECT_EQ(wizard.errorCount(), 0);
    EXPECT_EQ(wizard.createCount(), 20);
    EXPECT_EQ(wizard.reportRow(0).value(QStringLiteral("statusKey")).toString(),
              QStringLiteral("ok"));
    EXPECT_EQ(wizard.reportRow(0).value(QStringLiteral("rowNumber")).toInt(), 2);
    EXPECT_TRUE(wizard.canCommit());

    wizard.commit();
    EXPECT_EQ(wizard.step(), QStringLiteral("summary"));
    EXPECT_EQ(wizard.summary().value(QStringLiteral("created")).toInt(), 20);
    EXPECT_EQ(playerCount(), 20);
    EXPECT_EQ(squadChanged.count(), 1);
    EXPECT_TRUE(wizard.canUndo());

    wizard.undoImport();
    EXPECT_TRUE(wizard.undone());
    EXPECT_FALSE(wizard.canUndo());
    EXPECT_EQ(wizard.summary().value(QStringLiteral("deleted")).toInt(), 20);
    EXPECT_EQ(playerCount(), 0);
    EXPECT_EQ(squadChanged.count(), 2);

    wizard.reset();
    EXPECT_EQ(wizard.step(), QStringLiteral("type"));
    EXPECT_TRUE(wizard.summary().isEmpty());
}

// Packet P16 decision 8: without a position column the default position is asked for.
TEST_F(ImportWizardViewModelTest, FileWithoutPositionAsksForTheDefault) {
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "no-position.csv"));
    wizard.goToMapping();
    EXPECT_TRUE(wizard.defaultPositionRequired());
    EXPECT_EQ(wizard.mappingErrors().size(), 1);
    wizard.validate(); // refused while the mapping has errors
    EXPECT_EQ(wizard.step(), QStringLiteral("mapping"));
    wizard.setDefaultPosition(QStringLiteral("cm"));
    EXPECT_TRUE(wizard.mappingErrors().isEmpty());
    ASSERT_NO_FATAL_FAILURE(validate(wizard));
    EXPECT_EQ(wizard.warningCount(), 3);
    EXPECT_EQ(wizard.reportRow(0).value(QStringLiteral("statusKey")).toString(),
              QStringLiteral("warning"));
    EXPECT_FALSE(wizard.reportRow(0).value(QStringLiteral("message")).toString().isEmpty());
    wizard.commit();
    EXPECT_EQ(playerCount(), 3);
}

// Packet P16 decision 9: the order switch of a full-name column, with the split preview.
TEST_F(ImportWizardViewModelTest, FullNameOrderAndSplitPreview) {
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "quotes-commas.csv"));
    wizard.goToMapping();
    EXPECT_TRUE(wizard.nameOrderVisible());
    EXPECT_EQ(wizard.nameOrder(), QStringLiteral("last_first"));
    auto preview = wizard.nameSplitPreview();
    ASSERT_EQ(preview.size(), 3);
    EXPECT_EQ(preview[0].toMap().value(QStringLiteral("lastName")).toString(),
              QStringLiteral("Nowak-Kowalski"));
    EXPECT_EQ(preview[1].toMap().value(QStringLiteral("lastName")).toString(),
              QStringLiteral("Źrebiec"));
    wizard.setNameOrder(QStringLiteral("first_last"));
    preview = wizard.nameSplitPreview();
    EXPECT_EQ(preview[1].toMap().value(QStringLiteral("firstName")).toString(),
              QStringLiteral("Źrebiec"));
    // A first-name column replaces the full name.
    wizard.setColumnField(0, QStringLiteral("first_name"));
    EXPECT_FALSE(wizard.nameOrderVisible());
    EXPECT_FALSE(wizard.mappingErrors().isEmpty()); // no last name now
}

TEST_F(ImportWizardViewModelTest, FieldOnAnotherColumnMoves) {
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "utf8-bom.csv"));
    wizard.goToMapping();
    wizard.setColumnField(3, QStringLiteral("shirt_number"));
    EXPECT_EQ(wizard.mapping()->value(3, "field").toString(), QStringLiteral("shirt_number"));
    EXPECT_TRUE(wizard.mapping()->value(2, "field").toString().isEmpty());
    EXPECT_TRUE(wizard.mappingErrors().isEmpty());
}

TEST_F(ImportWizardViewModelTest, WrongFileStaysOnTheFileStep) {
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "video.csv"));
    EXPECT_EQ(wizard.step(), QStringLiteral("file"));
    EXPECT_EQ(wizard.errorText(), viewmodels::ErrorPresenter::message(core::Error{
                                      core::ErrorCode::UnsupportedFormat, {}, "import_file_type"}));
    wizard.openPath(fixture("legacy.xls"));
    ASSERT_TRUE(waitIdle(wizard));
    EXPECT_EQ(wizard.errorText(), viewmodels::ErrorPresenter::message(core::Error{
                                      core::ErrorCode::UnsupportedFormat, {}, "import_xls"}));
}

TEST_F(ImportWizardViewModelTest, MultiSheetXlsxOffersTheSheets) {
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "multi-sheet.xlsx"));
    EXPECT_EQ(wizard.step(), QStringLiteral("preview"));
    EXPECT_EQ(wizard.sheetOptions().size(), 2);
    EXPECT_EQ(wizard.sheetIndex(), 1);
    EXPECT_EQ(wizard.rowCount(), 3);
    wizard.selectSheet(0);
    ASSERT_TRUE(waitIdle(wizard));
    EXPECT_EQ(wizard.sheetIndex(), 0);
    EXPECT_EQ(wizard.rowCount(), 0);
    EXPECT_FALSE(wizard.errorText().isEmpty());
    wizard.goToMapping(); // nothing to map on this sheet
    EXPECT_EQ(wizard.step(), QStringLiteral("preview"));
}

TEST_F(ImportWizardViewModelTest, InvalidRowsSkipOrAbort) {
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "unknown-positions.csv"));
    wizard.goToMapping();
    ASSERT_NO_FATAL_FAILURE(validate(wizard));
    EXPECT_EQ(wizard.errorCount(), 2);
    EXPECT_TRUE(wizard.canCommit());
    wizard.setSkipInvalid(false);
    EXPECT_FALSE(wizard.canCommit());
}

TEST_F(ImportWizardViewModelTest, SameFileAgainWarnsBeforeCommit) {
    {
        ImportWizardViewModel wizard;
        ASSERT_NO_FATAL_FAILURE(open(wizard, "utf8-bom.csv"));
        wizard.goToMapping();
        ASSERT_NO_FATAL_FAILURE(validate(wizard));
        wizard.commit();
        ASSERT_EQ(wizard.step(), QStringLiteral("summary"));
    }
    ImportWizardViewModel again;
    ASSERT_NO_FATAL_FAILURE(open(again, "utf8-bom.csv"));
    EXPECT_FALSE(again.duplicateWarning().isEmpty());
    EXPECT_TRUE(again.mappingRemembered());
    again.goToMapping();
    ASSERT_NO_FATAL_FAILURE(validate(again));
    EXPECT_EQ(again.skipCount(), 2);
    EXPECT_FALSE(again.canCommit()); // nothing new
    again.setUpdateExisting(true);
    EXPECT_EQ(again.updateCount(), 2);
    EXPECT_TRUE(again.canCommit());
}

TEST_F(ImportWizardViewModelTest, RoleWithoutImport) {
    registry().session().clear();
    ImportWizardViewModel wizard;
    EXPECT_FALSE(wizard.canImport());
    wizard.chooseType(QStringLiteral("squad"));
    EXPECT_EQ(wizard.step(), QStringLiteral("type"));
}

TEST_F(ImportWizardViewModelTest, SavesTheTemplates) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ImportWizardViewModel wizard;
    for (const QString format : {QStringLiteral("csv"), QStringLiteral("xlsx")}) {
        const QString path = dir.filePath(QStringLiteral("kadra.") + format);
        ASSERT_TRUE(wizard.saveTemplate(QUrl::fromLocalFile(path), format));
        // Twice: the copy of a resource must stay writable, so it can be replaced.
        ASSERT_TRUE(wizard.saveTemplate(QUrl::fromLocalFile(path), format));
        QFile saved(path);
        ASSERT_TRUE(saved.open(QIODevice::ReadOnly));
        QFile resource(QStringLiteral(":/fdss/import-templates/squad.") + format);
        ASSERT_TRUE(resource.open(QIODevice::ReadOnly));
        EXPECT_EQ(saved.readAll(), resource.readAll());
    }
}

} // namespace
