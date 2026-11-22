#include "core/util/Version.hpp"
#include "data/db/AppPaths.hpp"
#include "data/db/DatabaseManager.hpp"
#include "services/SetupService.hpp"
#include "viewmodels/AppLog.hpp"
#include "viewmodels/JsGarbageCollector.hpp"
#include "viewmodels/LanguageManager.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/StartupViewModel.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

#include <cstdlib>
#include <memory>
#include <utility>

Q_IMPORT_QML_PLUGIN(FootballDSSPlugin)

namespace {

// Opens the database and installs the registry. Also makes the first read of the data, so a file
// that opens but cannot be read gets the error screen too, not a main window that keeps failing.
fdss::core::Status installServices(const fdss::core::Result<QString>& path) {
    using fdss::viewmodels::ServiceRegistry;
    if (!path) {
        return std::unexpected(path.error());
    }
    auto database = fdss::data::DatabaseManager::open(*path);
    if (!database) {
        return std::unexpected(database.error());
    }
    ServiceRegistry::install(ServiceRegistry::create(std::move(*database)));
    if (const auto complete = ServiceRegistry::instance().setup().isSetupComplete(); !complete) {
        return std::unexpected(complete.error());
    }
    return {};
}

// The log file of this run; nullptr when there is none (stderr still has every message). After the
// lock: a second instance must not rotate away the file of the running one.
std::unique_ptr<fdss::viewmodels::AppLog> installLog(fdss::viewmodels::AppLog::Start start) {
    using fdss::viewmodels::AppLog;
    const auto directory = fdss::data::AppPaths::logDirectory();
    if (!directory) {
        qWarning("No log file: %s", directory.error().detail.c_str());
        return nullptr;
    }
    auto installed = AppLog::install(*directory, AppLog::kKeepFiles, AppLog::kMaxBytes, start);
    if (!installed) {
        qWarning("No log file: %s", installed.error().detail.c_str());
        return nullptr;
    }
    return std::move(*installed);
}

} // namespace

int main(int argc, char* argv[]) {
    fdss::viewmodels::useNonIncrementalGarbageCollector();
    QGuiApplication app(argc, argv);
    // Organization and application names decide where QSettings and AppDataLocation live.
    QGuiApplication::setOrganizationName(QStringLiteral("FootballDSS"));
    QGuiApplication::setApplicationName(QStringLiteral("FootballDSS"));
    QGuiApplication::setApplicationVersion(
        QString::fromStdString(fdss::core::toString(fdss::core::currentVersion())));

    const auto databasePath = fdss::data::AppPaths::defaultDatabasePath();
    const auto instance = fdss::viewmodels::StartupViewModel::lockInstance(databasePath);
    if (!instance) {
        // Appended, never rotated: the files may belong to the instance that runs.
        const auto log = installLog(fdss::viewmodels::AppLog::Start::Append);
        qCritical("Start refused: %s", instance.error().detail.c_str());
        QQmlApplicationEngine engine;
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
            [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
        engine.singletonInstance<fdss::viewmodels::LanguageManager*>(
            QStringLiteral("FootballDSS"), QStringLiteral("LanguageManager"));
        engine
            .singletonInstance<fdss::viewmodels::StartupViewModel*>(
                QStringLiteral("FootballDSS"), QStringLiteral("StartupViewModel"))
            ->setInstanceError(instance.error(), databasePath.value_or(QString{}));
        engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("AlreadyRunningView"));
        QGuiApplication::exec();
        return EXIT_FAILURE;
    }
    const auto log = installLog(fdss::viewmodels::AppLog::Start::NewFile);
    qInfo("FootballDSS %s started", qUtf8Printable(QGuiApplication::applicationVersion()));
    const auto services = installServices(databasePath);

    int exitCode = EXIT_FAILURE;
    {
        QQmlApplicationEngine engine;
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
            [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);

        // Creates the single LanguageManager instance now, so the stored language is active
        // before the first qsTr() in Main.qml is evaluated.
        engine.singletonInstance<fdss::viewmodels::LanguageManager*>(
            QStringLiteral("FootballDSS"), QStringLiteral("LanguageManager"));

        if (services) {
            engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
        } else {
            qCritical("Cannot open the database: %s", services.error().detail.c_str());
            auto* startup = engine.singletonInstance<fdss::viewmodels::StartupViewModel*>(
                QStringLiteral("FootballDSS"), QStringLiteral("StartupViewModel"));
            startup->setDatabaseError(services.error(), databasePath.value_or(QString{}));
            engine.loadFromModule(QStringLiteral("FootballDSS"),
                                  QStringLiteral("DatabaseErrorView"));
        }
        exitCode = QGuiApplication::exec();
    }
    // After the engine, whose viewmodels use the services. Not left to static destruction: that
    // would close the database after QGuiApplication is gone.
    fdss::viewmodels::ServiceRegistry::reset();
    return exitCode;
}
