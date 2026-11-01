// Loads Main.qml in the three database states and every view offscreen. Fails on any warning,
// critical or fatal message, when the Polish translation is not applied, or when preferences do not
// survive a restart.

#include "core/domain/Club.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/domain/Team.hpp"
#include "core/util/Error.hpp"
#include "data/db/DatabaseManager.hpp"
#include "services/AuthService.hpp"
#include "services/SetupService.hpp"
#include "viewmodels/AppSettings.hpp"
#include "viewmodels/AppState.hpp"
#include "viewmodels/LanguageManager.hpp"
#include "viewmodels/NavigationModel.hpp"
#include "viewmodels/ProfilePickerViewModel.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SetupWizardViewModel.hpp"
#include "viewmodels/StaffViewModel.hpp"
#include "viewmodels/StartupViewModel.hpp"

#include <QDir>
#include <QDirIterator>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMetaObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QVariantMap>
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
using fdss::viewmodels::AppState;
using fdss::viewmodels::LanguageManager;
using fdss::viewmodels::NavigationModel;
using fdss::viewmodels::ProfilePickerViewModel;
using fdss::viewmodels::ServiceRegistry;
using fdss::viewmodels::SetupWizardViewModel;
using fdss::viewmodels::StaffViewModel;
using fdss::viewmodels::StartupViewModel;

namespace {

constexpr auto kViewsDirectory = ":/qt/qml/FootballDSS/ui/views";
// Enough to run PBKDF2 through; the real count would only make the test slow.
constexpr int kPinIterations = 4;

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
// A critical message that a test provokes on purpose. Anything else stays a problem.
const char* g_expectedCritical = nullptr;
int g_expectedCriticalCount = 0;

void countingMessageHandler(QtMsgType type, const QMessageLogContext& context,
                            const QString& message) {
    if (type == QtDebugMsg || type == QtInfoMsg) {
        if (g_defaultHandler != nullptr) {
            g_defaultHandler(type, context, message);
        }
        return;
    }
    if (type == QtCriticalMsg) {
        const std::lock_guard lock(g_messagesMutex);
        if (g_expectedCritical != nullptr &&
            message.contains(QLatin1StringView(g_expectedCritical))) {
            ++g_expectedCriticalCount;
            return;
        }
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

void exerciseComponentGallery(QQmlApplicationEngine& engine, QQuickWindow* window,
                              const QString& language, const QString& theme,
                              const QString& screenshotDirectory) {
    constexpr std::array<const char*, 4> kTabNames{"buttons", "inputs", "table", "feedback"};

    window->setProperty("componentGalleryOpen", true);
    settle();

    auto* gallery = window->property("componentGallery").value<QQuickItem*>();
    if (gallery == nullptr) {
        fail("Main.qml did not instantiate ComponentGalleryView");
        window->setProperty("componentGalleryOpen", false);
        return;
    }
    auto* toasts =
        engine.singletonInstance<QObject*>(QStringLiteral("FootballDSS"), QStringLiteral("Toasts"));
    if (toasts == nullptr) {
        fail("Toasts singleton is not registered");
        window->setProperty("componentGalleryOpen", false);
        return;
    }

    const int tabCount = gallery->property("tabCount").toInt();
    if (tabCount != static_cast<int>(kTabNames.size())) {
        fail("unexpected tab count in ComponentGalleryView");
    }

    for (std::size_t tab = 0; tab < kTabNames.size(); ++tab) {
        gallery->setProperty("currentTabIndex", static_cast<int>(tab));
        settle();

        if (tab == kTabNames.size() - 1) {
            QMetaObject::invokeMethod(gallery, "openSampleDialog");
            QMetaObject::invokeMethod(gallery, "triggerSampleUndoToast");
            settle();
            if (!gallery->property("dialogOpen").toBool()) {
                fail("sample AppDialog did not open");
            }
            if (!toasts->property("active").toBool()) {
                fail("sample undo toast is not active");
            }
        }

        if (screenshotDirectory.isEmpty()) {
            window->grabWindow();
        } else {
            saveScreenshot(window, screenshotDirectory,
                           QStringLiteral("gallery-%1-%2-%3.png")
                               .arg(QString::fromLatin1(kTabNames.at(tab)), language, theme));
        }

        if (tab == kTabNames.size() - 1) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &press);
            QCoreApplication::sendEvent(window, &release);
            settle();
            if (gallery->property("dialogOpen").toBool()) {
                fail("Esc did not close AppDialog");
                QMetaObject::invokeMethod(gallery, "closeSampleDialog");
                settle();
            }

            const int beforeUndo = gallery->property("undoCount").toInt();
            QMetaObject::invokeMethod(toasts, "undo");
            settle();
            if (gallery->property("undoCount").toInt() != beforeUndo + 1) {
                fail("toast undo did not invoke its callback");
            }
            if (toasts->property("active").toBool()) {
                fail("toast remained active after undo");
            }
        }
    }

    gallery->setProperty("currentTabIndex", 0);
    window->setProperty("componentGalleryOpen", false);
    settle();
}

struct UiPreferences {
    QString language;
    bool darkTheme = true;
};

// Opens a new in-memory database and installs the registry on it, with the state brought about
// through the services themselves: nothing, a club with a logged-out Coach, or a logged-in Coach.
// Returns the database, owned by the registry, for the test that breaks it; nullptr on failure.
fdss::data::DatabaseManager* installRegistry(AppState::State state) {
    auto opened = fdss::data::DatabaseManager::openInMemory();
    if (!opened) {
        qWarning("qml_smoke: cannot open the in-memory database: %s",
                 opened.error().detail.c_str());
        return nullptr;
    }
    fdss::data::DatabaseManager* const database = opened->get();
    fdss::viewmodels::RegistryOptions options;
    options.pinIterations = kPinIterations;
    ServiceRegistry::install(ServiceRegistry::create(std::move(*opened), std::move(options)));
    if (state == AppState::State::NeedsSetup) {
        return database;
    }

    fdss::core::ClubDraft club;
    club.name = "KS Smoke";
    fdss::core::TeamDraft team;
    team.name = "Seniorzy";
    team.season = "2026/27";
    const auto coach =
        ServiceRegistry::instance().setup().completeSetup(club, team, "Jan Trener", "1234");
    if (!coach) {
        qWarning("qml_smoke: cannot seed the database: %s", coach.error().detail.c_str());
        return nullptr;
    }
    if (state == AppState::State::NeedsLogin) {
        ServiceRegistry::instance().auth().logout();
    }
    return database;
}

// The first item of a QML file, by the name of the file. Compiled QML types carry a suffix. Walks
// the visual tree: findChildren() from the window does not reach what a Repeater creates.
QQuickItem* findQmlItem(QQuickItem* item, const char* fileName) {
    if (QLatin1StringView(item->metaObject()->className())
            .startsWith(QLatin1StringView(fileName))) {
        return item;
    }
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        if (QQuickItem* found = findQmlItem(child, fileName)) {
            return found;
        }
    }
    return nullptr;
}

// A view asks for a section with arguments; Main.qml passes them on, and only the view shown gets
// them.
void exerciseNavigationArguments(QQuickWindow* window, NavigationModel* navigation) {
    auto* calendar = findQmlItem(window->contentItem(), "CalendarView");
    auto* training = findQmlItem(window->contentItem(), "TrainingView");
    if (calendar == nullptr || training == nullptr) {
        fail("the calendar or training view is not loaded");
        return;
    }
    const QVariantMap args{{QStringLiteral("sessionId"), 5}};

    QMetaObject::invokeMethod(calendar, "navigateRequested",
                              Q_ARG(QString, QStringLiteral("training")),
                              Q_ARG(QVariant, QVariant(args)));
    settle();
    if (navigation->currentKey() != QLatin1StringView("training")) {
        fail("navigateRequested did not change the section");
    }
    if (training->property("navigationArgs").toMap() != args) {
        fail("the view shown did not get the navigation arguments");
    }
    if (!calendar->property("navigationArgs").toMap().isEmpty()) {
        fail("a hidden view got the navigation arguments");
    }

    navigation->setCurrentIndex(0);
    settle();
    if (!training->property("navigationArgs").toMap().isEmpty()) {
        fail("a view kept its navigation arguments after it was left");
    }
}

// A key press and release at whatever has the focus in the window.
void pressKey(QQuickWindow* window, Qt::Key key, const QString& text = {}) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    settle();
}

void grabOrSave(QQuickWindow* window, const QString& directory, const QString& name) {
    if (directory.isEmpty()) {
        window->grabWindow();
    } else {
        saveScreenshot(window, directory, name);
    }
}

SetupWizardViewModel* wizardOf(QQuickWindow* window) {
    auto* view = window->property("setupWizard").value<QQuickItem*>();
    return view != nullptr ? view->property("viewModel").value<SetupWizardViewModel*>() : nullptr;
}

ProfilePickerViewModel* pickerOf(QQuickWindow* window) {
    auto* view = window->property("profilePicker").value<QQuickItem*>();
    return view != nullptr ? view->property("viewModel").value<ProfilePickerViewModel*>() : nullptr;
}

// Renders every step of the wizard, the errors included, and goes back to the first one with the
// fields empty again.
void exerciseWizardSteps(QQuickWindow* window, const QString& language, const QString& theme,
                         const QString& screenshotDirectory) {
    auto* wizard = wizardOf(window);
    if (wizard == nullptr) {
        fail("the wizard is not there in the state NeedsSetup");
        return;
    }
    const auto capture = [&](const QString& step) {
        settle();
        grabOrSave(window, screenshotDirectory,
                   QStringLiteral("wizard-%1-%2-%3.png").arg(step, language, theme));
    };

    capture(QStringLiteral("1-welcome"));
    wizard->next();
    if (wizard->next()) {
        fail("the wizard left the club step with empty names");
    }
    capture(QStringLiteral("2-club-errors"));
    wizard->setClubName(QStringLiteral("KS Łączna"));
    wizard->setTeamName(QStringLiteral("Seniorzy"));
    capture(QStringLiteral("2-club"));
    if (!wizard->next()) {
        fail("the wizard did not accept the club step");
    }
    wizard->setCoachName(QStringLiteral("Jan Trener"));
    wizard->setPin(QStringLiteral("1234"));
    wizard->setPinRepeat(QStringLiteral("1235"));
    if (wizard->next()) {
        fail("the wizard left the coach step with different PINs");
    }
    capture(QStringLiteral("3-coach-errors"));
    wizard->setPinRepeat(QStringLiteral("1234"));
    capture(QStringLiteral("3-coach"));
    if (!wizard->next()) {
        fail("the wizard did not accept the coach step");
    }
    capture(QStringLiteral("4-summary"));

    for (int step = wizard->currentStep(); step > 0; --step) {
        wizard->back();
    }
    wizard->setClubName({});
    wizard->setTeamName({});
    wizard->setCoachName({});
    wizard->setPin({});
    wizard->setPinRepeat({});
    settle();
    if (wizard->currentStep() != 0) {
        fail("Back did not lead to the first step of the wizard");
    }
}

// Renders the picker with the only profile there is, selected right away. No attempts: they would
// add up to a lockout over the languages and themes.
void exerciseProfilePicker(QQuickWindow* window, const QString& language, const QString& theme,
                           const QString& screenshotDirectory) {
    auto* picker = pickerOf(window);
    if (picker == nullptr) {
        fail("the picker is not there in the state NeedsLogin");
        return;
    }
    if (picker->staff()->rowCount() == 0) {
        fail("the picker shows no staff");
        return;
    }
    settle();
    grabOrSave(window, screenshotDirectory,
               QStringLiteral("picker-%1-%2.png").arg(language, theme));
}

// Drives the main window through both languages and both themes and, when a user is logged in,
// through every section. With a screenshot directory, saves the empty shell for every language x
// theme pair. Returns the non-default preferences it leaves behind, so a restart can be checked.
std::optional<UiPreferences> exerciseMainWindow(QQmlApplicationEngine& engine,
                                                AppState::State expectedState,
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
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    if (appState->state() != expectedState) {
        fail("AppState does not match the state of the database");
    }
    const bool ready = expectedState == AppState::State::Ready;
    settle();
    if ((window->property("shell").value<QQuickItem*>() != nullptr) != ready) {
        fail("the shell must exist exactly when a user is logged in");
    }

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
            if (!ready) {
                const QString theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
                if (expectedState == AppState::State::NeedsSetup) {
                    exerciseWizardSteps(window, language, theme, screenshotDirectory);
                } else {
                    exerciseProfilePicker(window, language, theme, screenshotDirectory);
                }
                continue;
            }
            for (const bool collapsed : {false, true}) {
                window->setProperty("sidebarCollapsed", collapsed);
                for (int row = 0; row < navigation->rowCount(); ++row) {
                    navigation->setCurrentIndex(row);
                    settle();
                }
            }
            const QString theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
            if (!screenshotDirectory.isEmpty()) {
                window->setProperty("sidebarCollapsed", false);
                navigation->setCurrentIndex(0);
                settle();
                saveScreenshot(window, screenshotDirectory,
                               QStringLiteral("shell-%1-%2.png").arg(language, theme));
                navigation->activate(QStringLiteral("settings"));
                settle();
                saveScreenshot(window, screenshotDirectory,
                               QStringLiteral("settings-staff-%1-%2.png").arg(language, theme));
            }
            // P03 spike: the Qt Graphs view is reachable only through Ctrl+Shift+G. Grab it
            // even without a screenshot directory, so the charts are actually rendered.
            window->setProperty("spikeChartsOpen", true);
            settle();
            if (screenshotDirectory.isEmpty()) {
                window->grabWindow();
            } else {
                saveScreenshot(window, screenshotDirectory,
                               QStringLiteral("spike-charts-%1-%2.png").arg(language, theme));
            }
            window->setProperty("spikeChartsOpen", false);
            exerciseComponentGallery(engine, window, language, theme, screenshotDirectory);
        }
    }
    if (ready) {
        exerciseNavigationArguments(window, navigation);
    }

    // Defaults are the system language and the dark theme; leave the opposite behind.
    const UiPreferences leftBehind{languageOrder.constFirst(), false};
    languages->setLanguage(leftBehind.language);
    settings->setDarkTheme(leftBehind.darkTheme);
    settle();
    return leftBehind;
}

// The way through the states on a live window: wizard, shell, logout, picker, wrong PIN, lockout on
// another profile, right PIN, shell. Goes through the real viewmodels, so the wiring of the views
// to AppState is covered too. Expects an empty database and a loaded Main.qml.
void exerciseStateChanges(QQmlApplicationEngine& engine) {
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    auto* toasts =
        engine.singletonInstance<QObject*>(QStringLiteral("FootballDSS"), QStringLiteral("Toasts"));
    if (engine.rootObjects().isEmpty()) {
        return;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    const auto shellLoaded = [window] {
        return window->property("shell").value<QQuickItem*>() != nullptr;
    };

    auto* wizard = wizardOf(window);
    if (wizard == nullptr) {
        fail("the wizard is not there");
        return;
    }
    // Enter = Next, and it does not get past a step that is not valid.
    pressKey(window, Qt::Key_Return);
    if (wizard->currentStep() != 1) {
        fail("Enter did not go to the next step of the wizard");
    }
    pressKey(window, Qt::Key_Return);
    if (wizard->currentStep() != 1 || wizard->clubNameError().isEmpty()) {
        fail("Enter did not stop at the club step with empty names");
    }
    wizard->back();
    settle();
    wizard->setClubName(QStringLiteral("KS Smoke"));
    wizard->setTeamName(QStringLiteral("Seniorzy"));
    wizard->setCoachName(QStringLiteral("Jan Trener"));
    wizard->setPin(QStringLiteral("1234"));
    wizard->setPinRepeat(QStringLiteral("1234"));

    // A login from the wizard builds the shell once: the one seen when the state changes stays.
    QPointer<QQuickItem> shellAtStateChange;
    const QMetaObject::Connection watcher = QObject::connect(
        appState, &AppState::stateChanged, appState, [&shellAtStateChange, window] {
            shellAtStateChange = window->property("shell").value<QQuickItem*>();
        });
    const bool created = wizard->create();
    QObject::disconnect(watcher);
    settle();
    if (!created || appState->state() != AppState::State::Ready || !shellLoaded()) {
        fail("the wizard did not lead to the shell");
    }
    if (window->property("setupWizard").value<QQuickItem*>() != nullptr) {
        fail("the wizard stayed after the setup");
    }
    if (shellAtStateChange == nullptr ||
        shellAtStateChange != window->property("shell").value<QQuickItem*>()) {
        fail("a login built the shell more than once");
    }
    if (!appState->can(QStringLiteral("ManageStaff"))) {
        fail("the coach of the wizard may not manage staff");
    }

    // A second profile to lock out, added through the Staff section of the Coach.
    auto* settingsView = findQmlItem(window->contentItem(), "SettingsView");
    auto* staffSection = settingsView != nullptr
                             ? settingsView->property("staffSection").value<QQuickItem*>()
                             : nullptr;
    auto* staff = staffSection != nullptr
                      ? staffSection->property("viewModel").value<StaffViewModel*>()
                      : nullptr;
    if (staff == nullptr) {
        fail("the coach has no Staff section");
        return;
    }
    const QString pin = QStringLiteral("1234");
    if (!staff->add(QStringLiteral("Anna Asystent"), QStringLiteral("assistant"), pin, pin)) {
        fail("the Staff section did not add a staff member");
    }
    const qint64 assistantId = staff->staff()->staff().back().id.value;
    settle();
    if (!staffSection->property("hasSelection").toBool()) {
        fail("adding a staff member lost the selected row");
    }
    if (!staffSection->property("ownRowSelected").toBool()) {
        fail("the first row of the table is not the coach");
    }

    // The dialogs of the section and of the top bar open and close without a warning.
    for (const char* opener : {"openAddDialog", "openEditDialog", "openResetPinDialog"}) {
        QMetaObject::invokeMethod(staffSection, opener);
        settle();
        window->grabWindow();
        pressKey(window, Qt::Key_Escape);
    }
    auto* topBar = findQmlItem(window->contentItem(), "TopBar");
    if (topBar == nullptr) {
        fail("the top bar is not there");
        return;
    }
    for (const char* opener : {"openUserMenu", "openChangePinDialog"}) {
        QMetaObject::invokeMethod(topBar, opener);
        settle();
        window->grabWindow();
        pressKey(window, Qt::Key_Escape);
    }

    // Renaming yourself reaches the top bar through AppState.
    if (!staff->update(staff->currentUserId(), QStringLiteral("Jan Nowy"),
                       QStringLiteral("coach"))) {
        fail("the Staff section did not rename the coach");
    }
    settle();
    if (appState->userName() != QLatin1StringView("Jan Nowy") || !shellLoaded()) {
        fail("renaming yourself did not update the session");
    }

    // An undo toast does not reach the next user.
    QMetaObject::invokeMethod(toasts, "show", Q_ARG(QVariant, QStringLiteral("Undo me")),
                              Q_ARG(QVariant, QStringLiteral("Undo")), Q_ARG(QVariant, QVariant{}));
    navigation->activate(QStringLiteral("settings"));
    window->setProperty("sidebarCollapsed", true);
    appState->logout();
    settle();
    if (appState->state() != AppState::State::NeedsLogin || shellLoaded()) {
        fail("logout did not remove the shell");
    }
    if (navigation->currentKey() != QLatin1StringView("dashboard")) {
        fail("logout did not go back to the dashboard");
    }
    if (toasts->property("active").toBool()) {
        fail("a toast of the last user stayed after the logout");
    }

    auto* picker = pickerOf(window);
    if (picker == nullptr) {
        fail("the picker is not there after a logout");
        return;
    }
    if (picker->staff()->rowCount() != 2) {
        fail("the picker does not show both profiles");
    }
    picker->select(assistantId);
    settle();
    // Esc clears, so what Enter then submits is an empty PIN, which is not counted.
    for (const Qt::Key digit : {Qt::Key_9, Qt::Key_9, Qt::Key_9, Qt::Key_9}) {
        pressKey(window, digit, QStringLiteral("9"));
    }
    pressKey(window, Qt::Key_Escape);
    pressKey(window, Qt::Key_Return);
    if (!picker->message().contains(QLatin1StringView("4"))) {
        fail("Esc did not clear the PIN, or Enter did not submit it");
    }
    // Typed with the keyboard and submitted with Enter: the first wrong PIN.
    for (const Qt::Key digit : {Qt::Key_9, Qt::Key_9, Qt::Key_9, Qt::Key_9}) {
        pressKey(window, digit, QStringLiteral("9"));
    }
    pressKey(window, Qt::Key_Return);
    const QString afterWrongPin = picker->message();
    if (afterWrongPin.isEmpty()) {
        fail("a wrong PIN says nothing");
    }
    window->grabWindow();
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (picker->locked()) {
            fail("the profile was locked too early");
        }
        picker->login(QStringLiteral("9999"));
    }
    settle();
    if (!picker->locked()) {
        fail("five wrong PINs did not lock the profile");
    }
    window->grabWindow();

    picker->select(picker->staff()->staff().front().id.value);
    settle();
    picker->login(pin);
    settle();
    if (appState->state() != AppState::State::Ready || !shellLoaded()) {
        fail("the right PIN did not lead to the shell");
    }
    if (window->property("profilePicker").value<QQuickItem*>() != nullptr) {
        fail("the picker stayed after the login");
    }
    window->setProperty("sidebarCollapsed", false);
    settle();
}

// One user logs in over another, with no logout in between. The state stays Ready, yet the views
// have to be built anew: they and their can() bindings belong to the last user.
void exerciseLoginOverSession(QQmlApplicationEngine& engine) {
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    const QPointer<QQuickItem> lastShell = window->property("shell").value<QQuickItem*>();
    const QPointer<QQuickItem> lastView = findQmlItem(window->contentItem(), "SettingsView");
    if (lastShell == nullptr || lastView == nullptr) {
        fail("the shell of the coach is not there");
        return;
    }
    if (lastView->property("staffSection").value<QQuickItem*>() == nullptr) {
        fail("the coach does not see the Staff section");
    }

    auto& auth = ServiceRegistry::instance().auth();
    fdss::core::StaffUserDraft draft;
    draft.name = "Anna Analityk";
    draft.role = fdss::core::Role::Analyst;
    const auto analyst = auth.addStaff(draft, "1234");
    if (!analyst) {
        qWarning("qml_smoke: cannot add the analyst: %s", analyst.error().detail.c_str());
        return;
    }
    navigation->activate(QStringLiteral("settings"));
    settle();
    const int lastSerial = appState->sessionSerial();

    const auto login = auth.login(*analyst, "1234");
    if (!login || login->status != fdss::services::PinCheckStatus::Accepted) {
        fail("the analyst cannot log in");
        return;
    }
    appState->refresh();
    settle();

    if (appState->state() != AppState::State::Ready) {
        fail("a login over a session left the state Ready");
    }
    if (appState->sessionSerial() == lastSerial) {
        fail("a login over a session did not change the serial");
    }
    if (lastShell != nullptr || lastView != nullptr) {
        fail("the views of the last user survived a login over a session");
    }
    auto* settingsView = findQmlItem(window->contentItem(), "SettingsView");
    if (window->property("shell").value<QQuickItem*>() == nullptr || settingsView == nullptr) {
        fail("a login over a session did not build the views anew");
    } else if (settingsView->property("staffSection").value<QQuickItem*>() != nullptr) {
        fail("the analyst sees the Staff section");
    }
    if (navigation->currentKey() != QLatin1StringView("dashboard")) {
        fail("a login over a session did not go back to the dashboard");
    }
    if (appState->can(QStringLiteral("ManageStaff")) || !appState->can(QStringLiteral("Import"))) {
        fail("can() does not answer for the analyst");
    }
    if (appState->userName() != QLatin1StringView("Anna Analityk")) {
        fail("the name of the last user stayed after a login over a session");
    }
}

// A logout does not depend on the database: with reads failing the session still ends, the shell
// goes, and the error is only reported. Breaks the database, so it runs last on this registry.
void exerciseLogoutOnUnreadableDatabase(QQmlApplicationEngine& engine,
                                        fdss::data::DatabaseManager& database) {
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    auto* toasts =
        engine.singletonInstance<QObject*>(QStringLiteral("FootballDSS"), QStringLiteral("Toasts"));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    const QPointer<QQuickItem> lastShell = window->property("shell").value<QQuickItem*>();
    if (lastShell == nullptr) {
        fail("there is no shell to log out of");
        return;
    }
    if (const auto broken = database.execSql(QStringLiteral("ALTER TABLE club RENAME TO club_gone"),
                                             "break the database");
        !broken) {
        qWarning("qml_smoke: cannot break the database: %s", broken.error().detail.c_str());
        return;
    }

    {
        const std::lock_guard lock(g_messagesMutex);
        g_expectedCritical = "no such table: club";
    }
    appState->logout();
    settle();
    {
        const std::lock_guard lock(g_messagesMutex);
        g_expectedCritical = nullptr;
        if (g_expectedCriticalCount != 1) {
            fail("the failed read was not logged exactly once");
        }
    }

    if (appState->state() != AppState::State::NeedsLogin) {
        fail("a logout with a failed read did not need a login");
    }
    if (!appState->userName().isEmpty() || !appState->roleName().isEmpty()) {
        fail("a logout with a failed read kept the user");
    }
    if (lastShell != nullptr || window->property("shell").value<QQuickItem*>() != nullptr) {
        fail("a logout with a failed read did not destroy the shell");
    }
    if (!toasts->property("active").toBool()) {
        fail("a logout with a failed read did not show the error");
    }
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

// The window main.cpp loads when the database cannot be opened: every error code needs a translated
// text, and the technical detail never reaches the user. Restores the language it found.
void exerciseDatabaseErrorView(QQmlEngine& engine, const QString& screenshotDirectory) {
    using fdss::core::Error;
    using fdss::core::ErrorCode;
    constexpr std::array kCodes{
        ErrorCode::NotADatabase, ErrorCode::DatabaseLocked, ErrorCode::IncompatibleVersion,
        ErrorCode::Io,           ErrorCode::Database,       ErrorCode::Internal};
    const char* const detail = "technical detail for the log";

    auto* languages = engine.singletonInstance<LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));
    auto* startup = engine.singletonInstance<StartupViewModel*>(QStringLiteral("FootballDSS"),
                                                                QStringLiteral("StartupViewModel"));
    QQmlComponent component(&engine, QStringLiteral("FootballDSS"),
                            QStringLiteral("DatabaseErrorView"));
    const std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    if (window == nullptr) {
        qWarning("qml_smoke: DatabaseErrorView is not a window: %s",
                 qUtf8Printable(component.errorString()));
        return;
    }

    const QString originalLanguage = languages->language();
    const QString samplePath = QStringLiteral("/sample/folder/club.sqlite");
    QStringList firstMessages;
    const QStringList available = languages->availableLanguages();
    for (const QString& language : available) {
        languages->setLanguage(language);
        for (const ErrorCode code : kCodes) {
            startup->setDatabaseError(Error{code, detail, {}}, samplePath);
            settle();
            const QString message = startup->errorMessage();
            if (message.isEmpty()) {
                fail("a startup error has no message");
            }
            if (message.contains(QLatin1StringView(detail))) {
                fail("a startup error shows the technical detail");
            }
            if (code == kCodes.front()) {
                firstMessages.append(message);
            }
        }
        window->grabWindow();
        if (!screenshotDirectory.isEmpty()) {
            startup->setDatabaseError(Error{kCodes.front(), detail, {}}, samplePath);
            settle();
            saveScreenshot(window, screenshotDirectory,
                           QStringLiteral("database-error-%1.png").arg(language));
        }
    }
    if (firstMessages.removeDuplicates() > 0) {
        fail("startup error messages are not translated");
    }
    languages->setLanguage(originalLanguage);
    settle();
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

#if defined(Q_OS_WIN)
    // Offscreen needs an explicit font directory because the Qt installation has no bundled fonts.
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR")) {
        const QByteArray windowsDirectory = qgetenv("WINDIR");
        qputenv("QT_QPA_FONTDIR",
                (windowsDirectory.isEmpty() ? QByteArray("C:/Windows") : windowsDirectory) +
                    "/Fonts");
    }
#endif

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
        for (const AppState::State state :
             {AppState::State::NeedsSetup, AppState::State::NeedsLogin, AppState::State::Ready}) {
            // Same order as src/main.cpp: the registry is installed before any QML loads, and goes
            // after the engine, whose singletons hold on to its services.
            fdss::data::DatabaseManager* const database = installRegistry(state);
            if (database == nullptr) {
                fail("cannot install the service registry");
                continue;
            }
            {
                QQmlApplicationEngine engine;
                leftBehind = exerciseMainWindow(engine, state, screenshotDirectory);
                if (state == AppState::State::NeedsSetup) {
                    exerciseStateChanges(engine);
                }
                if (state == AppState::State::Ready) {
                    exerciseDatabaseErrorView(engine, screenshotDirectory);
                    viewCount = instantiateEveryView(engine);
                    exerciseLoginOverSession(engine);
                    exerciseLogoutOnUnreadableDatabase(engine, *database);
                }
            }
            settle();
            ServiceRegistry::reset();
        }
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
