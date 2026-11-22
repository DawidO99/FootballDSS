#include "viewmodels/ImportWizardViewModel.hpp"

#include "ViewModelFixture.hpp"
#include "data/repositories/MetricRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "viewmodels/ImportTexts.hpp"
#include "viewmodels/RowsModel.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>

// The wizard with training and match files (packet P18).

using namespace fdss;
using namespace fdss::tests;
using fdss::viewmodels::ImportTexts;
using fdss::viewmodels::ImportWizardViewModel;

namespace {

QString fixture(const char* name) {
    return QStringLiteral(FDSS_IMPORT_FIXTURES_DIR "/%1").arg(QLatin1StringView(name));
}

bool waitIdle(const ImportWizardViewModel& wizard) {
    QElapsedTimer timer;
    timer.start();
    QCoreApplication::processEvents();
    while (wizard.busy() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return !wizard.busy();
}

class ImportActivityWizardTest : public ViewModelTest {
protected:
    void SetUp() override {
        ViewModelTest::SetUp();
        completeSetup();
        QSqlQuery query(m_db->database());
        query.exec(QStringLiteral("SELECT id FROM team"));
        ASSERT_TRUE(query.next());
        m_team = query.value(0).toLongLong();
        data::PlayerRepository players(*m_db);
        for (const auto& [first, last, number] :
             {std::tuple{"Jan", "Kowalski", 9}, std::tuple{"Piotr", "Nowak", 4},
              std::tuple{"Adam", "Nowak", 5}, std::tuple{"Łukasz", "Wiśniewski", 7}}) {
            core::PlayerDraft draft;
            draft.firstName = first;
            draft.lastName = last;
            draft.shirtNumber = number;
            ASSERT_TRUE(players.create(core::TeamId{m_team}, draft).has_value());
        }
    }

    [[nodiscard]] int count(const char* sql) const {
        QSqlQuery query(m_db->database());
        query.exec(QString::fromLatin1(sql));
        return query.next() ? query.value(0).toInt() : -1;
    }

    void toReport(ImportWizardViewModel& wizard, const char* type, const char* file) {
        wizard.setTeamId(m_team);
        wizard.chooseType(QString::fromLatin1(type));
        ASSERT_EQ(wizard.step(), QStringLiteral("file"));
        wizard.openPath(fixture(file));
        ASSERT_TRUE(waitIdle(wizard));
        ASSERT_EQ(wizard.step(), QStringLiteral("preview"));
        wizard.goToMapping();
        ASSERT_TRUE(wizard.mappingErrors().isEmpty());
        wizard.validate();
        ASSERT_TRUE(waitIdle(wizard));
        ASSERT_EQ(wizard.step(), QStringLiteral("report"));
    }

    qint64 m_team = 0;
};

TEST_F(ImportActivityWizardTest, ThreeTypes) {
    ImportWizardViewModel wizard;
    ASSERT_EQ(wizard.typeOptions().size(), 3);
    wizard.chooseType(QStringLiteral("training"));
    EXPECT_EQ(wizard.type(), QStringLiteral("training"));
    EXPECT_EQ(wizard.metricScope(), QStringLiteral("training"));
    wizard.reset();
    wizard.chooseType(QStringLiteral("matches"));
    EXPECT_EQ(wizard.metricScope(), QStringLiteral("match"));
    wizard.reset();
    wizard.chooseType(QStringLiteral("nothing"));
    EXPECT_EQ(wizard.step(), QStringLiteral("type"));
}

TEST_F(ImportActivityWizardTest, TrainingWithUnknownPlayersAndUndo) {
    ImportWizardViewModel wizard;
    SignalCount squadChanged(&wizard, &ImportWizardViewModel::squadChanged);
    SignalCount question(&wizard, &ImportWizardViewModel::undoConfirmationRequested);
    ASSERT_NO_FATAL_FAILURE(toReport(wizard, "training", "training-cp1250.csv"));
    EXPECT_EQ(wizard.mapping()->value(0, "field").toString(), QStringLiteral("date"));
    EXPECT_EQ(wizard.mapping()->value(3, "field").toString(), QStringLiteral("player"));
    EXPECT_EQ(wizard.okCount(), 36);
    EXPECT_EQ(wizard.warningCount(), 2); // unknown and ambiguous player
    EXPECT_EQ(wizard.errorCount(), 1);   // RPE 11
    EXPECT_EQ(wizard.createCount(), 36);
    EXPECT_EQ(wizard.reportRow(38).value(QStringLiteral("statusKey")).toString(),
              QStringLiteral("error"));
    EXPECT_FALSE(wizard.reportRow(38).value(QStringLiteral("message")).toString().isEmpty());
    EXPECT_FALSE(wizard.nameOrderVisible());
    EXPECT_TRUE(wizard.canCommit());

    // Unknown players added to the squad: one, without a position by default (P19 decision 7).
    wizard.setUnknownPlayers(QStringLiteral("create"));
    EXPECT_EQ(wizard.newPlayerCount(), 1);
    EXPECT_TRUE(wizard.nameOrderVisible());
    EXPECT_TRUE(wizard.defaultPositionRequired());
    EXPECT_TRUE(wizard.defaultPosition().isEmpty());
    EXPECT_EQ(wizard.createCount(), 37);
    EXPECT_TRUE(wizard.canCommit());
    // The row of the new player warns about its position as the squad import does.
    const auto messagesContain = [&wizard](const QString& text) {
        for (int row = 0; row < wizard.rowCount(); ++row) {
            if (wizard.reportRow(row).value(QStringLiteral("message")).toString().contains(text)) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(messagesContain(ImportTexts::positionWarning(false)));
    EXPECT_FALSE(messagesContain(ImportTexts::positionWarning(true)));
    wizard.setDefaultPosition(QStringLiteral("st"));
    EXPECT_TRUE(wizard.canCommit());
    EXPECT_TRUE(messagesContain(ImportTexts::positionWarning(true)));
    EXPECT_FALSE(messagesContain(ImportTexts::positionWarning(false)));

    wizard.commit();
    ASSERT_EQ(wizard.step(), QStringLiteral("summary"));
    EXPECT_EQ(wizard.summary().value(QStringLiteral("sessionsCreated")).toInt(), 12);
    EXPECT_EQ(wizard.summary().value(QStringLiteral("playersCreated")).toInt(), 1);
    EXPECT_FALSE(wizard.summaryText().isEmpty());
    EXPECT_EQ(count("SELECT count(*) FROM attendance"), 37);
    EXPECT_EQ(squadChanged.count(), 1);

    // Nothing was added after the import: no question.
    wizard.undoImport();
    EXPECT_EQ(question.count(), 0);
    EXPECT_TRUE(wizard.undone());
    EXPECT_EQ(wizard.summary().value(QStringLiteral("sessionsDeleted")).toInt(), 12);
    EXPECT_EQ(count("SELECT count(*) FROM training_session"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM player"), 4);
}

TEST_F(ImportActivityWizardTest, MatchesNameWhatIsNotImportedAndAskBeforeDeletingNotes) {
    ImportWizardViewModel wizard;
    SignalCount question(&wizard, &ImportWizardViewModel::undoConfirmationRequested);
    ASSERT_NO_FATAL_FAILURE(toReport(wizard, "matches", "matches-cp1250.csv"));
    EXPECT_FALSE(wizard.notImportedText().isEmpty());
    EXPECT_FALSE(wizard.suspensionNote().isEmpty());
    EXPECT_EQ(wizard.createCount(), 7);
    wizard.commit();
    ASSERT_EQ(wizard.step(), QStringLiteral("summary"));
    EXPECT_EQ(wizard.summary().value(QStringLiteral("matchesCreated")).toInt(), 3);
    EXPECT_FALSE(wizard.notImportedText().isEmpty());

    QSqlQuery note(m_db->database());
    ASSERT_TRUE(note.exec(QStringLiteral("INSERT INTO match_note (match_id, minute, text) "
                                         "SELECT id, 10, 'Notatka' FROM match LIMIT 1")));
    wizard.undoImport();
    EXPECT_EQ(question.count(), 1);
    EXPECT_FALSE(wizard.undone());
    EXPECT_EQ(count("SELECT count(*) FROM match"), 3);
    wizard.confirmUndo();
    EXPECT_TRUE(wizard.undone());
    EXPECT_EQ(count("SELECT count(*) FROM match"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM opponent"), 0);
}

TEST_F(ImportActivityWizardTest, CreateMetricMapsTheColumnToTheNewMetric) {
    ImportWizardViewModel wizard;
    wizard.setTeamId(m_team);
    wizard.chooseType(QStringLiteral("training"));
    wizard.openPath(fixture("training-cp1250.csv"));
    ASSERT_TRUE(waitIdle(wizard));
    wizard.goToMapping();
    ASSERT_TRUE(wizard.mapping()->value(7, "field").toString().isEmpty());
    int requested = -1;
    QString header;
    QObject::connect(&wizard, &ImportWizardViewModel::createMetricRequested,
                     [&](int column, const QString& text) {
                         requested = column;
                         header = text;
                     });
    wizard.setColumnField(7, QStringLiteral("create_metric"));
    EXPECT_EQ(requested, 7);
    EXPECT_EQ(header, QStringLiteral("Sprint 30 m"));
    EXPECT_TRUE(wizard.mapping()->value(7, "field").toString().isEmpty());

    // What the metric dialog does on Save.
    data::MetricRepository metrics(*m_db);
    core::MetricDefinitionDraft draft;
    draft.namePl = "Sprint 30 m";
    draft.scope = core::MetricScope::Training;
    ASSERT_TRUE(metrics.createDefinition(draft, "sprint_30_m").has_value());
    wizard.metricCreated(7);
    EXPECT_EQ(wizard.mapping()->value(7, "field").toString(), QStringLiteral("metric:sprint_30_m"));
    wizard.validate();
    ASSERT_TRUE(waitIdle(wizard));
    wizard.commit();
    EXPECT_EQ(count("SELECT count(*) FROM metric_value"), 4);
}

TEST_F(ImportActivityWizardTest, TemplatesOfEveryType) {
    ImportWizardViewModel wizard;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (const char* type : {"training", "matches"}) {
        for (const char* format : {"csv", "xlsx"}) {
            const QString target = dir.filePath(
                QStringLiteral("%1.%2").arg(QLatin1StringView(type), QLatin1StringView(format)));
            ASSERT_TRUE(wizard.saveTemplate(QUrl::fromLocalFile(target),
                                            QString::fromLatin1(format),
                                            QString::fromLatin1(type)));
            QFile saved(target);
            QFile resource(QStringLiteral(":/fdss/import-templates/%1.%2")
                               .arg(QLatin1StringView(type), QLatin1StringView(format)));
            ASSERT_TRUE(saved.open(QIODevice::ReadOnly));
            ASSERT_TRUE(resource.open(QIODevice::ReadOnly));
            EXPECT_EQ(saved.readAll(), resource.readAll());
        }
    }
}

} // namespace
