#include "core/util/Version.hpp"
#include "viewmodels/LanguageManager.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>

#include <cstdlib>

Q_IMPORT_QML_PLUGIN(FootballDSSPlugin)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    // Organization and application names decide where QSettings and AppDataLocation live.
    QGuiApplication::setOrganizationName(QStringLiteral("FootballDSS"));
    QGuiApplication::setApplicationName(QStringLiteral("FootballDSS"));
    QGuiApplication::setApplicationVersion(
        QString::fromStdString(fdss::core::toString(fdss::core::currentVersion())));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);

    // Creates the single LanguageManager instance now, so the stored language is active
    // before the first qsTr() in Main.qml is evaluated.
    engine.singletonInstance<fdss::viewmodels::LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));

    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    return QGuiApplication::exec();
}
