#include "viewmodels/ImportWizardViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/RowsModel.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <tuple>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

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
// P16 manual test: the default position starts as CM (ŚP), not the first of the list (GK).
TEST_F(ImportWizardViewModelTest, FileWithoutPositionAsksForTheDefault) {
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "no-position.csv"));
    wizard.goToMapping();
    EXPECT_TRUE(wizard.defaultPositionRequired());
    EXPECT_EQ(wizard.defaultPosition(), QStringLiteral("cm"));
    EXPECT_TRUE(wizard.mappingErrors().isEmpty());
    wizard.setDefaultPosition(QStringLiteral("st"));
    EXPECT_EQ(wizard.defaultPosition(), QStringLiteral("st"));
    wizard.setDefaultPosition(QStringLiteral("cm"));
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

// P16 manual test: another extension than csv/txt/tsv/xlsx is refused on the file step, and a text
// that is no table too.
TEST_F(ImportWizardViewModelTest, UnsupportedExtensionAndNonTableStayOnTheFileStep) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString markdown = dir.filePath(QStringLiteral("kadra.md"));
    ASSERT_TRUE(QFile::copy(fixture("utf8-bom.csv"), markdown));
    ImportWizardViewModel wizard;
    wizard.setTeamId(m_team);
    wizard.chooseType(QStringLiteral("squad"));
    wizard.openPath(markdown);
    ASSERT_TRUE(waitIdle(wizard));
    EXPECT_EQ(wizard.step(), QStringLiteral("file"));
    EXPECT_EQ(wizard.errorText(), viewmodels::ErrorPresenter::message(core::Error{
                                      core::ErrorCode::UnsupportedFormat, {}, "import_file_type"}));

    const QString notes = dir.filePath(QStringLiteral("notatki.txt"));
    QFile file(notes);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(
        "# Notatki z treningu\nDobra intensywność, trzeba popracować nad stałymi fragmentami.\n"
        "- rozgrzewka, 15 min\n- gra na małym polu\n");
    file.close();
    wizard.openPath(notes);
    ASSERT_TRUE(waitIdle(wizard));
    EXPECT_EQ(wizard.step(), QStringLiteral("file"));
    EXPECT_EQ(wizard.errorText(), viewmodels::ErrorPresenter::message(core::Error{
                                      core::ErrorCode::UnsupportedFormat, {}, "import_not_table"}));
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

TEST_F(ImportWizardViewModelTest, MappingAndOptionChangesInvalidateRunningValidation) {
    const std::array<std::function<void(ImportWizardViewModel&)>, 5> changes = {
        [](auto& wizard) { wizard.setColumnField(2, QString()); },
        [](auto& wizard) { wizard.setNameOrder(QStringLiteral("last_first")); },
        [](auto& wizard) { wizard.setDefaultPosition(QStringLiteral("cb")); },
        [](auto& wizard) { wizard.setUpdateExisting(true); },
        [](auto& wizard) { wizard.setSkipInvalid(false); }};
    for (const auto& change : changes) {
        ImportWizardViewModel wizard;
        ASSERT_NO_FATAL_FAILURE(open(wizard, "utf8-bom.csv"));
        wizard.goToMapping();
        wizard.validate();
        ASSERT_TRUE(wizard.busy());
        change(wizard);
        ASSERT_TRUE(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
        EXPECT_FALSE(wizard.busy());
        EXPECT_EQ(wizard.step(), QStringLiteral("mapping"));
        EXPECT_EQ(wizard.report()->rowCount(), 0);
        EXPECT_FALSE(wizard.canCommit());
        wizard.commit();
        EXPECT_EQ(playerCount(), 0);
        ASSERT_NO_FATAL_FAILURE(validate(wizard));
        EXPECT_TRUE(wizard.canCommit());
    }
}

// P16 review follow-up 5: the message names the line, which does not travel in Error::field.
TEST_F(ImportWizardViewModelTest, IgnoredColumnWithAnOpenQuoteStopsTheWholeWizard) {
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "unterminated-quote.csv"));
    EXPECT_EQ(wizard.step(), QStringLiteral("file"));
    EXPECT_EQ(wizard.errorText(),
              QCoreApplication::translate("fdss::viewmodels::ImportWizardViewModel",
                                          "The CSV has a quoted field that is never closed, "
                                          "starting on line %1. Close the quote in the file and "
                                          "import it again.")
                  .arg(2));
    EXPECT_NE(wizard.errorText(), viewmodels::ErrorPresenter::message(core::Error{
                                      core::ErrorCode::InvalidInput, {}, "import_csv_quote"}));
    wizard.goToMapping();
    wizard.validate();
    wizard.commit();
    EXPECT_EQ(wizard.step(), QStringLiteral("file"));
    EXPECT_FALSE(wizard.canCommit());
    EXPECT_EQ(wizard.report()->rowCount(), 0);
    EXPECT_EQ(playerCount(), 0);
}

// P16 review follow-up 1: a player added after the check refuses the commit; the wizard stays on
// the report with the row marked, and the next commit writes the plan now shown.
TEST_F(ImportWizardViewModelTest, SquadChangedAfterTheCheckStaysOnTheReport) {
    ImportWizardViewModel wizard;
    QStringList errors;
    QObject::connect(&wizard, &ImportWizardViewModel::errorOccurred,
                     [&errors](const QString& message) { errors.append(message); });
    ASSERT_NO_FATAL_FAILURE(open(wizard, "utf8-bom.csv"));
    wizard.goToMapping();
    ASSERT_NO_FATAL_FAILURE(validate(wizard));
    core::PlayerDraft draft;
    draft.firstName = "Łukasz";
    draft.lastName = "Żółkiewski";
    draft.primaryPosition = core::Position::St;
    data::PlayerRepository players(*m_db);
    ASSERT_TRUE(players.create(core::TeamId{m_team}, draft).has_value());

    wizard.commit();
    EXPECT_EQ(wizard.step(), QStringLiteral("report"));
    EXPECT_EQ(playerCount(), 1);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_EQ(errors.front(), viewmodels::ErrorPresenter::message(core::Error{
                                  core::ErrorCode::Conflict, {}, "import_matches_changed"}));
    const QString changed = QCoreApplication::translate(
        "fdss::viewmodels::ImportTexts", "the squad changed since the check — check again");
    EXPECT_TRUE(wizard.reportRow(0).value(QStringLiteral("details")).toString().contains(changed));
    EXPECT_EQ(wizard.reportRow(0).value(QStringLiteral("statusKey")).toString(),
              QStringLiteral("warning"));
    EXPECT_FALSE(wizard.reportRow(1).value(QStringLiteral("details")).toString().contains(changed));

    wizard.commit();
    EXPECT_EQ(wizard.step(), QStringLiteral("summary"));
    EXPECT_EQ(wizard.summary().value(QStringLiteral("created")).toInt(), 1);
    EXPECT_EQ(wizard.summary().value(QStringLiteral("skipped")).toInt(), 1);
    EXPECT_EQ(playerCount(), 2);
}

// P16 review follow-up 2: the report counts a number conflict as an error only for an update.
TEST_F(ImportWizardViewModelTest, NumberConflictCountsAsAnErrorOnlyForAnUpdate) {
    data::PlayerRepository players(*m_db);
    for (const auto& [first, last, number] :
         {std::tuple{"Jan", "Kowalski", 7}, std::tuple{"Adam", "Nowak", 9}}) {
        core::PlayerDraft draft;
        draft.firstName = first;
        draft.lastName = last;
        draft.shirtNumber = number;
        ASSERT_TRUE(players.create(core::TeamId{m_team}, draft).has_value());
    }
    ImportWizardViewModel wizard;
    ASSERT_NO_FATAL_FAILURE(open(wizard, "number-conflict.csv"));
    wizard.goToMapping();
    ASSERT_NO_FATAL_FAILURE(validate(wizard));
    wizard.setSkipInvalid(false);
    EXPECT_EQ(wizard.errorCount(), 0);
    EXPECT_TRUE(wizard.canCommit());
    wizard.setUpdateExisting(true);
    EXPECT_EQ(wizard.errorCount(), 1);
    EXPECT_EQ(wizard.reportRow(0).value(QStringLiteral("statusKey")).toString(),
              QStringLiteral("error"));
    EXPECT_FALSE(wizard.canCommit());
}

#ifdef Q_OS_UNIX
TEST_F(ImportWizardViewModelTest, TemplateWriteFailureKeepsTheOriginalFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("kadra.xlsx"));
    const QByteArray original("original user data");
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(original), original.size());
    }
    struct rlimit originalLimit {};
    ASSERT_EQ(getrlimit(RLIMIT_FSIZE, &originalLimit), 0);
    auto limited = originalLimit;
    limited.rlim_cur = 0;
    const auto previousHandler = std::signal(SIGXFSZ, SIG_IGN);
    const int applied = setrlimit(RLIMIT_FSIZE, &limited);
    ImportWizardViewModel wizard;
    // Creating the temporary file succeeds, but writing/committing its bytes fails with EFBIG.
    const bool saved =
        applied == 0 && wizard.saveTemplate(QUrl::fromLocalFile(path), QStringLiteral("xlsx"));
    const int restored = setrlimit(RLIMIT_FSIZE, &originalLimit);
    std::signal(SIGXFSZ, previousHandler);
    ASSERT_EQ(applied, 0);
    ASSERT_EQ(restored, 0);
    EXPECT_FALSE(saved);
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), original);
}
#endif

} // namespace
