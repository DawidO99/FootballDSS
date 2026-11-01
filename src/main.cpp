#include "core/util/Version.hpp"
#include "data/db/AppPaths.hpp"
#include "data/db/DatabaseManager.hpp"
#include "viewmodels/LanguageManager.hpp"
#include "viewmodels/StartupViewModel.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

#include <cstdlib>
#include <memory>

Q_IMPORT_QML_PLUGIN(FootballDSSPlugin)

namespace {

fdss::core::Result<std::unique_ptr<fdss::data::DatabaseManager>>
openDatabase(const fdss::core::Result<QString>& path) {
    if (!path) {
        return std::unexpected(path.error());
    }
    return fdss::data::DatabaseManager::open(*path);
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    // Organization and application names decide where QSettings and AppDataLocation live.
    QGuiApplication::setOrganizationName(QStringLiteral("FootballDSS"));
    QGuiApplication::setApplicationName(QStringLiteral("FootballDSS"));
    QGuiApplication::setApplicationVersion(
        QString::fromStdString(fdss::core::toString(fdss::core::currentVersion())));

    // Declared before the engine, so the database outlives everything QML creates.
    const auto databasePath = fdss::data::AppPaths::defaultDatabasePath();
    const auto database = openDatabase(databasePath);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);

    // Creates the single LanguageManager instance now, so the stored language is active
    // before the first qsTr() in Main.qml is evaluated.
    engine.singletonInstance<fdss::viewmodels::LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));

    if (database) {
        engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    } else {
        qCritical("Cannot open the database: %s", database.error().detail.c_str());
        auto* startup = engine.singletonInstance<fdss::viewmodels::StartupViewModel*>(
            QStringLiteral("FootballDSS"), QStringLiteral("StartupViewModel"));
        startup->setDatabaseError(database.error(), databasePath.value_or(QString{}));
        engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("DatabaseErrorView"));
    }
    return QGuiApplication::exec();
}
