// Loads Main.qml and every view offscreen. Fails on any warning, critical or fatal message,
// when the Polish translation is not applied, or when preferences do not survive a restart.

#include "viewmodels/AppSettings.hpp"
#include "viewmodels/LanguageManager.hpp"
#include "viewmodels/NavigationModel.hpp"

#include <QDir>
#include <QDirIterator>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QSettings>
#include <QtQml/QQmlExtensionPlugin>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

Q_IMPORT_QML_PLUGIN(FootballDSSPlugin)

using fdss::viewmodels::AppSettings;
using fdss::viewmodels::LanguageManager;
using fdss::viewmodels::NavigationModel;

namespace {

constexpr auto kViewsDirectory = ":/qt/qml/FootballDSS/ui/views";

// Platform noise that is not a QML problem. Every entry needs a comment explaining why.
#if defined(Q_OS_MACOS)
constexpr std::array<const char*, 1> kAllowedMessages{
    // Qt's macOS font backend reports the one-time cost of resolving the generic Sans Serif
    // alias. Font fallback succeeds; this is a performance notice, not a failed QML binding.
    "Replace uses of missing font family \"Sans Serif\" with one that exists to avoid this cost."};
#else
constexpr std::array<const char*, 0> kAllowedMessages{};
#endif

QtMessageHandler g_defaultHandler = nullptr;
std::atomic<int> g_problemCount = 0;
std::mutex g_messagesMutex;
std::vector<QString> g_messages;
int g_warningCount = 0;

void countingMessageHandler(QtMsgType type, const QMessageLogContext& context,
                            const QString& message) {
    if (type == QtDebugMsg || type == QtInfoMsg) {
        if (g_defaultHandler != nullptr) {
            g_defaultHandler(type, context, message);
        }
        return;
    }
    bool allowedMessage = false;
    // Never apply platform exceptions to warnings from our QML or to critical/fatal messages.
    if (type == QtWarningMsg && qstrcmp(context.category, "qt.qpa.fonts") == 0 &&
        message.startsWith(QLatin1StringView("Populating font family aliases took "))) {
        for (const char* allowed : kAllowedMessages) {
            if (message.contains(QLatin1StringView(allowed))) {
                allowedMessage = true;
                break;
            }
        }
    }
    const char* typeName = type == QtWarningMsg    ? "warning"
                           : type == QtCriticalMsg ? "critical"
                                                   : "fatal";
    const QString diagnostic =
        QStringLiteral("type=%1 category=%2 file=%3:%4 message=\"%5\"%6")
            .arg(QString::fromLatin1(typeName),
                 QString::fromUtf8(context.category != nullptr ? context.category : "<unknown>"),
                 QString::fromUtf8(context.file != nullptr ? context.file : "<unknown>"))
            .arg(context.line)
            .arg(message,
                 allowedMessage ? QStringLiteral(" [allowed platform warning]") : QString{});
    const std::lock_guard lock(g_messagesMutex);
    std::fprintf(stderr, "qml_smoke: %s\n", qUtf8Printable(diagnostic));
    std::fflush(stderr);
    g_messages.push_back(diagnostic);
    if (type == QtWarningMsg) {
        ++g_warningCount;
    }
    if (!allowedMessage) {
        ++g_problemCount;
    }
}

void fail(const char* reason) {
    std::fprintf(stderr, "qml_smoke: %s\n", reason);
    ++g_problemCount;
}

void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void saveScreenshot(QQuickWindow* window, const QString& directory, const QString& name) {
    const QString path = QDir(directory).filePath(name);
    if (!window->grabWindow().save(path)) {
        qWarning("qml_smoke: cannot save screenshot %s", qUtf8Printable(path));
    }
}

struct UiPreferences {
    QString language;
    bool darkTheme = true;
};

// Drives the main window through every section in both languages and both themes.
// With a screenshot directory, saves the empty shell for every language x theme pair.
// Returns the non-default preferences it leaves behind, so a restart can be checked.
std::optional<UiPreferences> exerciseMainWindow(QQmlApplicationEngine& engine,
                                                const QString& screenshotDirectory) {
    // Same order as src/main.cpp: the translator is installed before Main.qml is loaded.
    auto* languages = engine.singletonInstance<LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));
    auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                            QStringLiteral("AppSettings"));
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        fail("Main.qml did not load");
        return std::nullopt;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        fail("Main.qml root is not a window");
        return std::nullopt;
    }
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    if (navigation == nullptr) {
        fail("Main.qml does not expose the 'navigation' property");
        return std::nullopt;
    }
    settle();

    // Current language last, so every setLanguage() below is a live switch.
    QStringList languageOrder = languages->availableLanguages();
    languageOrder.removeOne(languages->language());
    languageOrder.append(languages->language());

    for (const QString& language : std::as_const(languageOrder)) {
        languages->setLanguage(language);
        if (language == QLatin1StringView("pl") &&
            navigation->data(navigation->index(0), NavigationModel::TitleRole).toString() ==
                QLatin1StringView("Dashboard")) {
            fail("Polish translation is not applied (FootballDSS_pl.qm missing or empty?)");
        }
        for (const bool dark : {true, false}) {
            settings->setDarkTheme(dark);
            for (const bool collapsed : {false, true}) {
                window->setProperty("sidebarCollapsed", collapsed);
                for (int row = 0; row < navigation->rowCount(); ++row) {
                    navigation->setCurrentIndex(row);
                    settle();
                }
            }
            if (!screenshotDirectory.isEmpty()) {
                window->setProperty("sidebarCollapsed", false);
                navigation->setCurrentIndex(0);
                settle();
                saveScreenshot(
                    window, screenshotDirectory,
                    QStringLiteral("shell-%1-%2.png")
                        .arg(language, dark ? QStringLiteral("dark") : QStringLiteral("light")));
            }
        }
    }

    // Defaults are the system language and the dark theme; leave the opposite behind.
    const UiPreferences leftBehind{languageOrder.constFirst(), false};
    languages->setLanguage(leftBehind.language);
    settings->setDarkTheme(leftBehind.darkTheme);
    settle();
    return leftBehind;
}

// A fresh engine creates fresh singletons, which must read the preferences back from QSettings.
void verifyPreferencesSurviveRestart(const UiPreferences& expected) {
    QQmlEngine engine;
    const auto* languages = engine.singletonInstance<LanguageManager*>(
        QStringLiteral("FootballDSS"), QStringLiteral("LanguageManager"));
    const auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                                  QStringLiteral("AppSettings"));
    if (languages->language() != expected.language) {
        fail("language was not restored after restart");
    }
    if (settings->darkTheme() != expected.darkTheme) {
        fail("theme was not restored after restart");
    }
}

// Instantiates every file in ui/views/ on its own, so new views are covered automatically.
int instantiateEveryView(QQmlEngine& engine) {
    int viewCount = 0;
    QDirIterator it(QString::fromLatin1(kViewsDirectory), {QStringLiteral("*.qml")}, QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc") + path));
        const std::unique_ptr<QObject> view(component.create());
        if (view == nullptr) {
            qWarning("qml_smoke: cannot create %s: %s", qUtf8Printable(path),
                     qUtf8Printable(component.errorString()));
        }
        settle();
        ++viewCount;
    }
    return viewCount;
}

} // namespace

int main(int argc, char* argv[]) {
    // Deterministic offscreen rendering without a GPU.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    g_defaultHandler = qInstallMessageHandler(countingMessageHandler);

    int viewCount = 0;
    {
        QGuiApplication app(argc, argv);
        QGuiApplication::setOrganizationName(QStringLiteral("FootballDSS"));
        // Own application name: the test never touches the real user settings.
        QGuiApplication::setApplicationName(QStringLiteral("FootballDSS-qml-smoke"));
        QSettings().clear();

        // Optional: qml_smoke --screenshots <dir> saves the shell images used in the thesis.
        QString screenshotDirectory;
        const QStringList arguments = QCoreApplication::arguments();
        if (const qsizetype flag = arguments.indexOf(QStringLiteral("--screenshots"));
            flag >= 0 && flag + 1 < arguments.size()) {
            screenshotDirectory = arguments.at(flag + 1);
        }

        std::optional<UiPreferences> leftBehind;
        {
            QQmlApplicationEngine engine;
            leftBehind = exerciseMainWindow(engine, screenshotDirectory);
            viewCount = instantiateEveryView(engine);
        }
        settle();
        if (leftBehind) {
            verifyPreferencesSurviveRestart(*leftBehind);
        }
        QSettings().clear();

        if (viewCount == 0) {
            fail("no views found in ui/views/");
        }
    } // Include application teardown messages in the final result.
    qInstallMessageHandler(g_defaultHandler);
    const std::lock_guard lock(g_messagesMutex);
    std::fprintf(stderr, "qml_smoke: %d warning(s), %zu diagnostic message(s)\n", g_warningCount,
                 g_messages.size());
    for (const QString& diagnostic : g_messages) {
        std::fprintf(stderr, "  %s\n", qUtf8Printable(diagnostic));
    }
    std::printf("qml_smoke: %d views, %d problem(s)\n", viewCount, g_problemCount.load());
    return g_problemCount == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
