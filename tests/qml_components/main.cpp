// Qt Quick Test runner for the UI kit behaviour that qml_smoke cannot see (bindings surviving user
// edits, re-validation, calendar limits). Test cases are the tst_*.qml files next to this file.

#include "viewmodels/JsGarbageCollector.hpp"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QSettings>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuickTest/quicktest.h>

Q_IMPORT_QML_PLUGIN(FootballDSSPlugin)

int main(int argc, char* argv[]) {
    // As the app does.
    fdss::viewmodels::useNonIncrementalGarbageCollector();
    // Deterministic offscreen rendering without a GPU.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

#if defined(Q_OS_WIN)
    // Offscreen needs an explicit font directory because the Qt installation has no bundled fonts.
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR")) {
        const QByteArray windowsDirectory = qgetenv("WINDIR");
        qputenv("QT_QPA_FONTDIR",
                (windowsDirectory.isEmpty() ? QByteArray("C:/Windows") : windowsDirectory) +
                    "/Fonts");
    }
#endif

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("FootballDSS"));
    // Own application name: the test never touches the real user settings.
    QGuiApplication::setApplicationName(QStringLiteral("FootballDSS-qml-components"));
    QSettings().clear();

    const int result = quick_test_main(argc, argv, "qml_components", FDSS_QML_TESTS_DIR);
    QSettings().clear();
    return result;
}
