// Loads Main.qml in the three database states and every view offscreen. Fails on any warning,
// critical or fatal message, when the Polish translation is not applied, or when preferences do not
// survive a restart.

#include "../services/FakeClock.hpp"
#include "core/algorithms/Formations.hpp"
#include "core/algorithms/LineupValidator.hpp"
#include "core/demo/DemoGenerator.hpp"
#include "core/domain/Club.hpp"
#include "core/domain/Player.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/domain/Team.hpp"
#include "core/domain/Validation.hpp"
#include "core/util/Error.hpp"
#include "data/db/DatabaseManager.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/AuthService.hpp"
#include "services/CalendarService.hpp"
#include "services/DemoDataService.hpp"
#include "services/LineupService.hpp"
#include "services/LoadService.hpp"
#include "services/MetricService.hpp"
#include "services/Session.hpp"
#include "services/SetupService.hpp"
#include "services/SquadService.hpp"
#include "services/TrainingService.hpp"
#include "viewmodels/AppSettings.hpp"
#include "viewmodels/AppState.hpp"
#include "viewmodels/AvailabilityViewModel.hpp"
#include "viewmodels/CalendarViewModel.hpp"
#include "viewmodels/CalendarWeekModel.hpp"
#include "viewmodels/CapabilityModel.hpp"
#include "viewmodels/ChangePinViewModel.hpp"
#include "viewmodels/DashboardViewModel.hpp"
#include "viewmodels/ImportWizardViewModel.hpp"
#include "viewmodels/JsGarbageCollector.hpp"
#include "viewmodels/LanguageManager.hpp"
#include "viewmodels/LoadTexts.hpp"
#include "viewmodels/LoadViewModel.hpp"
#include "viewmodels/MatchDayViewModel.hpp"
#include "viewmodels/MatchEditorViewModel.hpp"
#include "viewmodels/MatchReportViewModel.hpp"
#include "viewmodels/MetricDefinitionsViewModel.hpp"
#include "viewmodels/MetricEntryViewModel.hpp"
#include "viewmodels/NavigationModel.hpp"
#include "viewmodels/PlayerCardViewModel.hpp"
#include "viewmodels/PlayerEditorViewModel.hpp"
#include "viewmodels/PlayerLoadViewModel.hpp"
#include "viewmodels/PlayerMatchesModel.hpp"
#include "viewmodels/PlayerMetricsViewModel.hpp"
#include "viewmodels/PreMatchViewModel.hpp"
#include "viewmodels/ProfilePickerViewModel.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SessionEditorViewModel.hpp"
#include "viewmodels/SetupWizardViewModel.hpp"
#include "viewmodels/SquadViewModel.hpp"
#include "viewmodels/StaffViewModel.hpp"
#include "viewmodels/StartupViewModel.hpp"
#include "viewmodels/TrainingSessionViewModel.hpp"

#include <QColor>
#include <QDate>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLineF>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSet>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QStyleHints>
#include <QThread>
#include <QVariantMap>
#include <QtQml/QQmlExtensionPlugin>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

Q_IMPORT_QML_PLUGIN(FootballDSSPlugin)

using fdss::viewmodels::AppSettings;
using fdss::viewmodels::AppState;
using fdss::viewmodels::AvailabilityViewModel;
using fdss::viewmodels::CalendarViewModel;
using fdss::viewmodels::ChangePinViewModel;
using fdss::viewmodels::ImportWizardViewModel;
using fdss::viewmodels::LanguageManager;
using fdss::viewmodels::LoadViewModel;
using fdss::viewmodels::MatchDayViewModel;
using fdss::viewmodels::MatchEditorViewModel;
using fdss::viewmodels::MatchReportViewModel;
using fdss::viewmodels::MetricDefinitionsViewModel;
using fdss::viewmodels::MetricEntryViewModel;
using fdss::viewmodels::NavigationModel;
using fdss::viewmodels::PlayerCardViewModel;
using fdss::viewmodels::PlayerEditorViewModel;
using fdss::viewmodels::PlayerLoadViewModel;
using fdss::viewmodels::PlayerMatchesModel;
using fdss::viewmodels::PlayerMetricsViewModel;
using fdss::viewmodels::PreMatchViewModel;
using fdss::viewmodels::ProfilePickerViewModel;
using fdss::viewmodels::ServiceRegistry;
using fdss::viewmodels::SessionEditorViewModel;
using fdss::viewmodels::SetupWizardViewModel;
using fdss::viewmodels::SquadViewModel;
using fdss::viewmodels::StaffViewModel;
using fdss::viewmodels::StartupViewModel;
using fdss::viewmodels::TrainingSessionViewModel;

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
// Owned by the installed registry, so a lockout can end without waiting for it.
fdss::tests::FakeClock* g_clock = nullptr;

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

// Polishes the layouts and renders: the geometry read after it is the one on screen.
// processEvents() alone reaches a frame only when the render loop's timer has fired (see
// exerciseTileScrolling).
void renderFrame(QQuickWindow* window) { window->grabWindow(); }

void saveScreenshot(QQuickWindow* window, const QString& directory, const QString& name) {
    const QString path = QDir(directory).filePath(name);
    if (!window->grabWindow().save(path)) {
        qWarning("qml_smoke: cannot save screenshot %s", qUtf8Printable(path));
    }
}

QQuickItem* findByObjectName(QQuickItem* item, const QString& name);

// P11 review: hovering the first and the last day of the sample LoadChart, and the day whose
// session and match have the same load: the tooltip names that day and puts each value under its
// own series (found by the day's index, not by the value).
void expectLoadChartTooltips(QQuickWindow* window, QQuickItem* gallery) {
    auto* chart = findByObjectName(gallery, QStringLiteral("galleryLoadChart"));
    auto* view =
        chart != nullptr ? findByObjectName(chart, QStringLiteral("loadChartView")) : nullptr;
    auto* tooltip =
        chart != nullptr ? findByObjectName(chart, QStringLiteral("chartTooltip")) : nullptr;
    if (view == nullptr || tooltip == nullptr) {
        fail("the gallery has no LoadChart with a tooltip");
        return;
    }
    const QVariantMap sample = gallery->property("sampleLoad").toMap();
    const QVariantList labels = sample.value(QStringLiteral("labels")).toList();
    const QVariantList sessions = sample.value(QStringLiteral("sessions")).toList();
    const QVariantList matches = sample.value(QStringLiteral("matches")).toList();
    const int equalDay = gallery->property("sampleEqualDay").toInt();
    if (labels.isEmpty() || sessions.value(equalDay) != matches.value(equalDay)) {
        fail("the sample load has no day with equal series");
        return;
    }
    const auto move = [window](QPointF scene) {
        QMouseEvent event(QEvent::MouseMove, scene, scene, window->mapToGlobal(scene), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &event);
        settle();
    };
    renderFrame(window);
    const QRectF area = view->property("plotArea").toRectF();
    const auto days = static_cast<qreal>(labels.size());
    for (const int day : {0, static_cast<int>(labels.size()) - 1, equalDay}) {
        move(view->mapToScene(
            QPointF(area.x() + ((day + 0.5) * area.width() / days), area.center().y())));
        if (chart->property("hoveredIndex").toInt() != day || !tooltip->isVisible()) {
            fail("hovering a day of the load chart does not pick that day");
            continue;
        }
        if (tooltip->property("title").toString() != labels.at(day).toString()) {
            fail("the load chart tooltip names another day");
        }
        const QVariantList rows = tooltip->property("rows").toList();
        const QVariantMap first = rows.value(0).toMap();
        const QVariantMap second = rows.value(1).toMap();
        if (first.value(QStringLiteral("key")) != QStringLiteral("sessions") ||
            first.value(QStringLiteral("shape")) != QStringLiteral("bar") ||
            first.value(QStringLiteral("value")).toString() != sessions.at(day).toString() ||
            second.value(QStringLiteral("key")) != QStringLiteral("matches") ||
            second.value(QStringLiteral("shape")) != QStringLiteral("barOutline") ||
            second.value(QStringLiteral("value")).toString() != matches.at(day).toString()) {
            fail("the load chart tooltip puts a value under the wrong series");
        }
    }
    // Away from the chart: the tooltip goes.
    move(QPointF(0, 0));
    if (tooltip->isVisible() || chart->property("hoveredIndex").toInt() != -1) {
        fail("the load chart tooltip stays after the pointer left");
    }
}

void exerciseComponentGallery(QQmlApplicationEngine& engine, QQuickWindow* window,
                              const QString& language, const QString& theme,
                              const QString& screenshotDirectory) {
    constexpr std::array<const char*, 6> kTabNames{"buttons", "inputs",  "table",
                                                   "charts",  "tactics", "feedback"};

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

        if (QLatin1StringView(kTabNames.at(tab)) == QLatin1StringView("charts")) {
            expectLoadChartTooltips(window, gallery);
        }
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
    auto clock = std::make_unique<fdss::tests::FakeClock>();
    g_clock = clock.get();
    options.clock = std::move(clock);
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

// Switches to the other language and back. True when the text changed on the way and came back.
template <class Read> bool expectFollowsLanguage(QQmlApplicationEngine& engine, Read read) {
    auto* languages = engine.singletonInstance<LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));
    const QString first = languages->language();
    const QString text = read();
    languages->setLanguage(first == QLatin1StringView("pl") ? QStringLiteral("en")
                                                            : QStringLiteral("pl"));
    settle();
    const bool changed = read() != text;
    languages->setLanguage(first);
    settle();
    return changed && read() == text;
}

// Keys and the active focus of items exist only in the focus window: a window without it (Xvfb
// without a window manager, another window on top; qml_smoke --without-focus) is activated first,
// as a user does by clicking into it. False when it does not get the focus within a second.
bool activateWindow(QQuickWindow* window) {
    if (QGuiApplication::focusWindow() != window) {
        window->requestActivate();
        QElapsedTimer waited;
        waited.start();
        while (QGuiApplication::focusWindow() != window && waited.elapsed() < 1000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    }
    return QGuiApplication::focusWindow() == window;
}

// A key press and release at whatever has the focus in the window (activated first).
void pressKey(QQuickWindow* window, Qt::Key key, const QString& text = {}) {
    activateWindow(window);
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

void typeText(QQuickWindow* window, const QString& text) {
    for (const QChar character : text) {
        pressKey(window, static_cast<Qt::Key>(character.toUpper().unicode()), QString(character));
    }
}

// Fills the form that a dialog opened on its first field, from the keyboard only. Nothing presses
// Enter yet.
void typePlayer(QQuickWindow* window, const QString& firstName, const QString& lastName,
                const QString& number = {}) {
    typeText(window, firstName);
    pressKey(window, Qt::Key_Tab);
    typeText(window, lastName);
    if (!number.isEmpty()) {
        pressKey(window, Qt::Key_Tab);
        typeText(window, number);
    }
}

void expectFlag(const QObject* object, const char* property, bool expected, const char* reason) {
    if (object->property(property).toBool() != expected) {
        fail(reason);
    }
}

// The first item of that object name, also in a dialog: the visual tree holds the overlay too.
QQuickItem* findByObjectName(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) {
        return item;
    }
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        if (QQuickItem* found = findByObjectName(child, name)) {
            return found;
        }
    }
    return nullptr;
}

// A left click in the middle of the item, through the window like a real mouse.
void clickItem(QQuickWindow* window, QQuickItem* item) {
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = window->mapToGlobal(center);
    QMouseEvent press(QEvent::MouseButtonPress, center, center, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, center, center, global, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    settle();
}

// The label wraps inside its width, a word too long for a line included.
void expectWrapsInside(QQuickItem* label, const char* reason) {
    if (label == nullptr || label->property("contentWidth").toReal() > label->width()) {
        fail(reason);
    }
}

// The worst the squad screens get at the minimum window: a card with very long notes and a long
// word must scroll, and a form with every error must keep its buttons in the window. Then New Year.
void exerciseMinimumWindow(QQuickWindow* window, NavigationModel* navigation, QQuickItem* view,
                           QQuickItem* card, SquadViewModel* squad,
                           const QString& screenshotDirectory) {
    constexpr int kMinimumWidth = 1280;
    constexpr int kMinimumHeight = 720;
    constexpr int kNoteLines = 80;
    constexpr qsizetype kLongWordLength = 500;
    const QSize originalSize = window->size();
    window->resize(kMinimumWidth, kMinimumHeight);
    settle();
    const auto capture = [&](const char* name) {
        settle();
        grabOrSave(window, screenshotDirectory, QString::fromLatin1(name) + QStringLiteral(".png"));
    };

    QStringList lines;
    for (int line = 1; line <= kNoteLines; ++line) {
        lines.append(QStringLiteral("Wiersz %1").arg(line));
    }
    lines.append(QString(kLongWordLength, u'x'));
    fdss::core::PlayerDraft draft;
    draft.firstName = "Dlugie";
    draft.lastName = "Notatki";
    draft.notes = lines.join(u'\n').toStdString();
    const auto added =
        ServiceRegistry::instance().squad().addPlayer(fdss::core::TeamId{squad->teamId()}, draft);
    if (!added) {
        fail("the player with long notes was not added");
        window->resize(originalSize);
        return;
    }

    navigation->activate(QStringLiteral("squad"),
                         QVariantMap{{QStringLiteral("playerId"), qint64{added->value}}});
    capture("squad-card-long-notes");
    auto* page = findByObjectName(card, QStringLiteral("overviewPage"));
    auto* notes = findByObjectName(card, QStringLiteral("playerData_notes"));
    if (page == nullptr || notes == nullptr) {
        fail("the overview page of the card is not all there");
        window->resize(originalSize);
        return;
    }
    if (page->property("contentHeight").toReal() <= page->height() || page->height() <= 0) {
        fail("very long notes do not make the card scroll");
    }
    expectWrapsInside(notes, "a long word in the notes leaves the card");
    if (notes->mapToItem(page, QPointF(notes->width(), 0)).x() > page->width()) {
        fail("the notes are wider than the card");
    }

    // The keyboard gets to the end of the notes from the Back button, which has the focus.
    pressKey(window, Qt::Key_PageDown);
    if (page->property("contentY").toReal() <= 0) {
        fail("PageDown does not scroll the card");
    }
    page->setProperty("contentY", page->property("contentHeight").toReal() - page->height());
    capture("squad-card-long-notes-end");
    const qreal notesBottom =
        notes->mapToItem(window->contentItem(), QPointF(0, notes->height())).y();
    if (notesBottom > kMinimumHeight) {
        fail("the end of the notes cannot be scrolled into the window");
    }

    // A long description can be read to its end under the table.
    auto* availability = card->property("availabilityModel").value<AvailabilityViewModel*>();
    if (!availability->add(QStringLiteral("injury"), QStringLiteral("2026-10-01"), {},
                           QString(kLongWordLength, u'y'))) {
        fail("the entry with a long description was not added");
    }
    card->findChild<QObject*>(QStringLiteral("entriesTable"))->setProperty("currentIndex", 0);
    page->setProperty("contentY", 0);
    capture("squad-card-long-description");
    expectWrapsInside(findByObjectName(card, QStringLiteral("selectedDescription")),
                      "a long word in the description leaves the card");

    QMetaObject::invokeMethod(view, "closeCard");
    settle();

    // Every error of the form at once.
    QObject* const dialogObject = view->property("playerEditor").value<QObject*>();
    auto* editor = dialogObject->property("viewModel").value<PlayerEditorViewModel*>();
    QMetaObject::invokeMethod(view, "openAddDialog");
    settle();
    editor->setShirtNumber(fdss::core::kMaxShirtNumber + 1);
    editor->setBirthYear(fdss::core::kMinBirthYear - 1);
    editor->setSecondaryPositions({editor->primaryPosition()});
    if (editor->save()) {
        fail("a form with every error was saved");
    }
    if (editor->firstNameError().isEmpty() || editor->lastNameError().isEmpty() ||
        editor->shirtNumberError().isEmpty() || editor->birthYearError().isEmpty() ||
        editor->secondaryPositionsError().isEmpty()) {
        fail("the form does not show every error");
    }
    capture("squad-editor-all-errors");
    const qreal dialogTop = dialogObject->property("y").toReal();
    const qreal dialogHeight = dialogObject->property("height").toReal();
    if (dialogTop < 0 || dialogTop + dialogHeight > kMinimumHeight) {
        fail("the form with every error leaves the minimum window");
    }
    auto* body = findByObjectName(window->contentItem(), QStringLiteral("dialogBody"));
    if (body == nullptr || body->height() <= 0) {
        fail("the form with every error has no room left");
    }
    pressKey(window, Qt::Key_Escape);
    settle();

    // The limit of the birth year follows New Year in a form opened again in the same session.
    constexpr auto kToNextYear = std::chrono::days{61};
    g_clock->advance(kToNextYear);
    QMetaObject::invokeMethod(view, "openAddDialog");
    settle();
    auto* birthYear = findByObjectName(window->contentItem(), QStringLiteral("birthYearInput"));
    if (birthYear == nullptr || birthYear->property("to").toInt() != editor->maxBirthYear() ||
        editor->maxBirthYear() != 2027 - fdss::core::kYoungestPlayerAge) {
        fail("the birth year field did not follow New Year");
    }
    pressKey(window, Qt::Key_Escape);
    settle();

    window->resize(originalSize);
    settle();
}

// The way of the squad on a live window: an empty table, players added from the keyboard, a taken
// shirt number, archiving with undo, a card opened by navigation arguments, and the dialogs of the
// card. A real Ctrl+N would need Qt Test or private API here, so the shortcut is activated directly
// and its enabled state is checked instead.
void exerciseSquad(QQmlApplicationEngine& engine, const QString& screenshotDirectory) {
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    auto* toasts =
        engine.singletonInstance<QObject*>(QStringLiteral("FootballDSS"), QStringLiteral("Toasts"));
    navigation->activate(QStringLiteral("squad"));
    settle();

    auto* view = findQmlItem(window->contentItem(), "SquadView");
    auto* card = findQmlItem(window->contentItem(), "PlayerCardView");
    if (view == nullptr || card == nullptr) {
        fail("the squad view or the player card is not loaded");
        return;
    }
    auto* squad = view->property("viewModel").value<SquadViewModel*>();
    auto* editorDialog = view->property("playerEditor").value<QObject*>();
    auto* editor = editorDialog != nullptr
                       ? editorDialog->property("viewModel").value<PlayerEditorViewModel*>()
                       : nullptr;
    auto* addShortcut = view->findChild<QObject*>(QStringLiteral("addShortcut"));
    auto* playersTable = view->findChild<QObject*>(QStringLiteral("playersTable"));
    auto* backShortcut = card->findChild<QObject*>(QStringLiteral("backShortcut"));
    if (squad == nullptr || editor == nullptr || addShortcut == nullptr ||
        playersTable == nullptr || backShortcut == nullptr) {
        fail("the squad view is not all there");
        return;
    }
    const auto capture = [&](const char* name) {
        settle();
        grabOrSave(window, screenshotDirectory, QString::fromLatin1(name) + QStringLiteral(".png"));
    };
    const auto openAddDialog = [&] {
        QMetaObject::invokeMethod(addShortcut, "activated");
        settle();
        expectFlag(editorDialog, "visible", true, "Ctrl+N did not open the player dialog");
        expectFlag(addShortcut, "enabled", false, "Ctrl+N stays on over the player dialog");
    };

    capture("squad-empty");
    expectFlag(addShortcut, "enabled", true, "the coach has no Ctrl+N in the squad");

    // Done when: three players from the keyboard only.
    for (const auto& [first, last] :
         {std::pair{"Jan", "Nowak"}, std::pair{"Adam", "Lis"}, std::pair{"Piotr", "Zych"}}) {
        openAddDialog();
        typePlayer(window, QString::fromLatin1(first), QString::fromLatin1(last));
        pressKey(window, Qt::Key_Return);
        expectFlag(editorDialog, "visible", false, "Enter did not save the player");
    }
    if (squad->players()->rowCount() != 3) {
        fail("the keyboard did not add three players");
    }

    // Done when: a duplicate shirt number shows an error on the field.
    openAddDialog();
    typePlayer(window, QStringLiteral("Ewa"), QStringLiteral("Bak"), QStringLiteral("7"));
    pressKey(window, Qt::Key_Return);
    expectFlag(editorDialog, "visible", false, "Enter did not save the player with a number");
    openAddDialog();
    typePlayer(window, QStringLiteral("Ola"), QStringLiteral("Bak"), QStringLiteral("7"));
    pressKey(window, Qt::Key_Return);
    expectFlag(editorDialog, "visible", true, "a taken shirt number closed the dialog");
    if (editor->shirtNumberError().isEmpty()) {
        fail("a taken shirt number shows no error on the field");
    }
    capture("squad-editor-duplicate");
    pressKey(window, Qt::Key_Escape);
    expectFlag(editorDialog, "visible", false, "Esc did not close the player dialog");

    // Text the field refused must not be saved as the last valid value.
    openAddDialog();
    typePlayer(window, QStringLiteral("Ola"), QStringLiteral("Bak"), QStringLiteral("150"));
    expectFlag(editorDialog, "primaryEnabled", false, "Save stays on over a refused number");
    pressKey(window, Qt::Key_Return);
    expectFlag(editorDialog, "visible", true, "Enter saved over a refused number");
    pressKey(window, Qt::Key_Escape);
    openAddDialog();
    expectFlag(editorDialog, "primaryEnabled", true, "the refused number stayed in the next form");
    pressKey(window, Qt::Key_Escape);
    if (squad->players()->rowCount() != 4) {
        fail("a failed save changed the list");
    }

    capture("squad-list");
    if (!expectFollowsLanguage(engine, [squad] {
            return squad->rowAt(0).value(QStringLiteral("status")).toString();
        })) {
        fail("the status in the squad table stayed in the old language");
    }

    // Done when: archive + undo.
    playersTable->setProperty("currentIndex", 0);
    settle();
    QMetaObject::invokeMethod(view, "archiveSelected");
    settle();
    if (squad->players()->rowCount() != 3 || !toasts->property("active").toBool()) {
        fail("archiving did not hide the player and offer an undo");
    }
    QMetaObject::invokeMethod(toasts, "undo");
    settle();
    if (squad->players()->rowCount() != 4) {
        fail("the undo toast did not bring the player back");
    }

    // An archived player: on request only, with the Archived badge, and Restore brings it back.
    playersTable->setProperty("currentIndex", 0);
    settle();
    const qint64 archivedId = squad->rowAt(0).value(QStringLiteral("playerId")).toLongLong();
    QMetaObject::invokeMethod(view, "archiveSelected");
    QMetaObject::invokeMethod(toasts, "dismiss");
    settle();
    expectFlag(view, "hasSelection", false, "a player gone from the table stays selected");
    squad->setShowArchived(true);
    settle();
    if (squad->players()->rowCount() != 4) {
        fail("Show archived did not show the archived player");
    }
    playersTable->setProperty("currentIndex", squad->rowOf(archivedId));
    capture("squad-archived");
    if (!view->property("selectedPlayer").toMap().value(QStringLiteral("archived")).toBool()) {
        fail("the archived player cannot be selected");
    }
    QMetaObject::invokeMethod(view, "restoreSelected");
    settle();
    squad->setShowArchived(false);
    settle();
    if (squad->players()->rowCount() != 4) {
        fail("Restore did not bring the player back");
    }

    // An empty table because of the filters offers to clear them.
    squad->setSearchText(QStringLiteral("nikogo takiego"));
    capture("squad-no-match");
    QMetaObject::invokeMethod(view, "clearFilters");
    settle();
    if (squad->players()->rowCount() != 4) {
        fail("Clear filters did not bring the players back");
    }

    // The whole form by Tab: the list of secondary positions keeps Esc for itself, and Enter saves
    // from the notes too.
    openAddDialog();
    typePlayer(window, QStringLiteral("Iza"), QStringLiteral("Wrona"));
    for (int field = 0; field < 4; ++field) {
        pressKey(window, Qt::Key_Tab);
    }
    pressKey(window, Qt::Key_Space, QStringLiteral(" "));
    pressKey(window, Qt::Key_Space, QStringLiteral(" "));
    capture("squad-editor-positions");
    if (editor->secondaryPositions() != QStringList{QStringLiteral("gk")}) {
        fail("Space in the list did not choose a secondary position");
    }
    pressKey(window, Qt::Key_Escape);
    expectFlag(editorDialog, "visible", true, "Esc in the list closed the whole dialog");
    pressKey(window, Qt::Key_Tab);
    pressKey(window, Qt::Key_Tab);
    typeText(window, QStringLiteral("x"));
    if (editor->notes() != QLatin1StringView("x")) {
        fail("Tab did not lead through the form to the notes");
    }
    pressKey(window, Qt::Key_Return);
    expectFlag(editorDialog, "visible", false, "Enter in the notes did not save the player");
    if (editor->notes() != QLatin1StringView("x") || squad->players()->rowCount() != 5) {
        fail("Enter in the notes did not add the player");
    }

    // Iza has GK as a secondary position: muted, and in the accent color under the group filter.
    auto* theme =
        engine.singletonInstance<QObject*>(QStringLiteral("FootballDSS"), QStringLiteral("Theme"));
    const auto secondaryCodeColor = [&] {
        settle();
        window->grabWindow();
        auto* code = findByObjectName(view, QStringLiteral("secondaryCode_GK"));
        return code != nullptr ? code->property("color").value<QColor>() : QColor{};
    };
    fdss::core::PlayerDraft goalkeeper;
    goalkeeper.firstName = "Olek";
    goalkeeper.lastName = "Bramka";
    goalkeeper.shirtNumber = 50;
    goalkeeper.primaryPosition = fdss::core::Position::Gk;
    if (!ServiceRegistry::instance()
             .squad()
             .addPlayer(fdss::core::TeamId{squad->teamId()}, goalkeeper)
             .has_value()) {
        fail("the goalkeeper was not added");
    }
    squad->reload();
    if (secondaryCodeColor() != theme->property("textMuted").value<QColor>()) {
        fail("a secondary position is not muted without a group filter");
    }
    squad->setPositionGroup(QStringLiteral("goalkeeper"));
    const QColor filteredColor = secondaryCodeColor();
    capture("squad-secondary-filter");
    if (filteredColor != theme->property("accent").value<QColor>()) {
        fail("the secondary position of the filter is not in the accent color");
    }
    if (squad->players()->rowCount() != 2) {
        fail("the group filter does not take secondary positions into account");
    }
    squad->setPositionGroup(QStringLiteral("all"));
    if (secondaryCodeColor() != theme->property("textMuted").value<QColor>()) {
        fail("a secondary position stayed highlighted after the group filter");
    }

    // navigationArgs.playerId opens the card, also when the section is entered that way.
    const qint64 playerId = squad->rowAt(0).value(QStringLiteral("playerId")).toLongLong();
    navigation->activate(QStringLiteral("training"));
    settle();
    expectFlag(addShortcut, "enabled", false, "Ctrl+N of the squad works in another section");
    navigation->activate(QStringLiteral("squad"),
                         QVariantMap{{QStringLiteral("playerId"), playerId}});
    settle();
    expectFlag(view, "cardOpen", true, "navigationArgs.playerId did not open the card");
    auto* cardModel = card->property("viewModel").value<PlayerCardViewModel*>();
    auto* availability = card->property("availabilityModel").value<AvailabilityViewModel*>();
    if (cardModel == nullptr || availability == nullptr) {
        fail("the player card is not all there");
        return;
    }
    if (cardModel->playerId() != playerId || cardModel->name().isEmpty()) {
        fail("the card does not show the player of the navigation arguments");
    }
    expectFlag(backShortcut, "enabled", true, "Esc does not lead back from the card");
    expectFlag(addShortcut, "enabled", false, "Ctrl+N works on the card, without its button");

    // Availability, the dialogs on top of the card, and Esc staying with the dialogs.
    capture("squad-card-empty");
    if (!availability->add(QStringLiteral("injury"), QStringLiteral("2026-10-01"), {},
                           QStringLiteral("Skręcona kostka"))) {
        fail("the coach could not add an availability entry");
    }
    if (cardModel->statusKey() != QLatin1StringView("injured")) {
        fail("the card did not follow a new availability entry");
    }
    if (squad->rowAt(squad->rowOf(playerId)).value(QStringLiteral("statusKey")).toString() !=
        QLatin1StringView("injured")) {
        fail("the squad table did not follow a new availability entry");
    }
    card->findChild<QObject*>(QStringLiteral("entriesTable"))->setProperty("currentIndex", 0);
    capture("squad-card");

    auto* availabilityDialog = card->property("availabilityEditor").value<QObject*>();
    QMetaObject::invokeMethod(card, "openEditAvailability");
    capture("squad-availability-dialog");
    expectFlag(availabilityDialog, "visible", true, "the availability dialog did not open");
    expectFlag(backShortcut, "enabled", false, "Esc would leave the card under a dialog");
    pressKey(window, Qt::Key_Escape);
    expectFlag(availabilityDialog, "visible", false, "Esc did not close the availability dialog");

    auto* returnDialog = card->property("returnEditor").value<QObject*>();
    QMetaObject::invokeMethod(card, "openMarkReturned");
    capture("squad-return-dialog");
    expectFlag(returnDialog, "visible", true, "the return dialog did not open");
    pressKey(window, Qt::Key_Return);
    expectFlag(returnDialog, "visible", false, "Enter did not mark the return");
    if (cardModel->statusKey() != QLatin1StringView("available")) {
        fail("marking the return did not change the status");
    }

    QMetaObject::invokeMethod(card, "editRequested", Q_ARG(double, double(playerId)));
    settle();
    expectFlag(editorDialog, "visible", true, "Edit on the card did not open the player dialog");
    expectFlag(backShortcut, "enabled", false, "Esc would leave the card under the player dialog");
    pressKey(window, Qt::Key_Escape);

    QMetaObject::invokeMethod(backShortcut, "activated");
    settle();
    expectFlag(view, "cardOpen", false, "Esc did not lead back to the list");
    if (playersTable->property("currentIndex").toInt() != squad->rowOf(playerId)) {
        fail("coming back from the card lost the selected player");
    }

    exerciseMinimumWindow(window, navigation, view, card, squad, screenshotDirectory);

    // Enter in the search of the top bar, from another section: the squad opens with the text in
    // its own search field, and the top bar is empty for the next one.
    navigation->setCurrentIndex(0);
    settle();
    auto* globalSearch = findByObjectName(window->contentItem(), QStringLiteral("globalSearch"));
    if (globalSearch == nullptr) {
        fail("the top bar has no search field");
        return;
    }
    globalSearch->forceActiveFocus(Qt::ShortcutFocusReason);
    typeText(window, QStringLiteral("Lis"));
    pressKey(window, Qt::Key_Return);
    settle();
    if (navigation->currentKey() != QLatin1StringView("squad")) {
        fail("Enter in the top bar search did not open the squad");
    }
    if (squad->searchText() != QLatin1StringView("Lis")) {
        fail("the squad did not search for the text of the top bar");
    }
    if (!globalSearch->property("text").toString().isEmpty()) {
        fail("the top bar search kept its text");
    }
    QMetaObject::invokeMethod(view, "clearFilters");

    navigation->setCurrentIndex(0);
    settle();
}

// Replaces the text of a DateField, which has the focus, from the keyboard only.
void retypeDate(QQuickWindow* window, const QQuickItem* field, const QDate& date) {
    const qsizetype length = field->property("displayText").toString().size();
    for (qsizetype i = 0; i < length; ++i) {
        pressKey(window, Qt::Key_Backspace);
    }
    typeText(window, date.toString(field->property("displayFormat").toString()));
}

// The items of one day of the shown week, by the day from Monday.
QVariantList itemsOfDay(const CalendarViewModel* calendar, int day) {
    return calendar->week()
        ->index(day)
        .data(fdss::viewmodels::CalendarWeekModel::ItemsRole)
        .toList();
}

void exerciseCalendar(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database,
                      const QString& screenshotDirectory) {
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    auto* toasts =
        engine.singletonInstance<QObject*>(QStringLiteral("FootballDSS"), QStringLiteral("Toasts"));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    navigation->activate(QStringLiteral("calendar"));
    settle();

    auto* view = findQmlItem(window->contentItem(), "CalendarView");
    if (view == nullptr) {
        fail("the calendar view is not loaded");
        return;
    }
    auto* calendar = view->property("viewModel").value<CalendarViewModel*>();
    auto* matchDialog = view->property("matchEditor").value<QObject*>();
    auto* sessionDialog = view->property("sessionEditor").value<QObject*>();
    auto* choiceDialog = view->property("addChoiceDialog").value<QObject*>();
    auto* deleteDialog = view->property("deleteSessionDialog").value<QObject*>();
    auto* addShortcut = view->findChild<QObject*>(QStringLiteral("addShortcut"));
    if (calendar == nullptr || matchDialog == nullptr || sessionDialog == nullptr ||
        choiceDialog == nullptr || deleteDialog == nullptr || addShortcut == nullptr) {
        fail("the calendar view is not all there");
        return;
    }
    auto* matchEditor = matchDialog->property("viewModel").value<MatchEditorViewModel*>();
    auto* sessionEditor = sessionDialog->property("viewModel").value<SessionEditorViewModel*>();
    const auto find = [&](const char* name) {
        return findByObjectName(window->contentItem(), QString::fromLatin1(name));
    };
    const auto capture = [&](const char* name) {
        settle();
        grabOrSave(window, screenshotDirectory, QString::fromLatin1(name) + QStringLiteral(".png"));
    };
    const auto chooseFromShortcut = [&](bool session) {
        QMetaObject::invokeMethod(addShortcut, "activated");
        settle();
        expectFlag(choiceDialog, "visible", true, "Ctrl+N did not ask which item to add");
        expectFlag(addShortcut, "enabled", false, "Ctrl+N stays on over the choice");
        if (session) {
            pressKey(window, Qt::Key_Tab);
        }
        pressKey(window, Qt::Key_Return);
        expectFlag(choiceDialog, "visible", false, "the choice did not close");
    };

    capture("calendar-empty");
    expectFlag(addShortcut, "enabled", true, "the coach has no Ctrl+N in the calendar");

    // The week has the same empty state as the list, with its button for the coach.
    view->setProperty("mode", QStringLiteral("week"));
    capture("calendar-week-empty");
    auto* weekEmptyState = findByObjectName(view, QStringLiteral("weekEmptyState"));
    auto* weekGrid = findByObjectName(view, QStringLiteral("weekGrid"));
    if (weekEmptyState == nullptr || weekGrid == nullptr) {
        fail("the week page is not all there");
        return;
    }
    expectFlag(weekEmptyState, "active", true, "an empty calendar has no empty state in the week");
    expectFlag(weekGrid, "visible", false, "an empty calendar shows an empty grid");
    if (weekEmptyState->property("item")
            .value<QObject*>()
            ->property("actionText")
            .toString()
            .isEmpty()) {
        fail("the empty week has no button for the coach");
    }
    view->setProperty("mode", QStringLiteral("list"));
    settle();

    // Done when: a match from the keyboard, with the first opponent added on the way.
    // The squad test moved the clock on, so these dates are well ahead of it: a Saturday and the
    // Tuesday before it, in the week of Monday 2030-01-07.
    chooseFromShortcut(false);
    expectFlag(matchDialog, "visible", true, "the choice did not open the match form");
    expectFlag(matchDialog, "newOpponentShown", true, "no opponents, and no field to add one");
    typeText(window, QStringLiteral("Lech"));
    pressKey(window, Qt::Key_Return);
    expectFlag(matchDialog, "visible", true, "Enter in the new opponent field left the form");
    expectFlag(matchDialog, "newOpponentShown", false, "the added opponent left its field open");
    if (matchEditor->opponentName() != QLatin1StringView("Lech")) {
        fail("the new opponent was not chosen");
    }
    if (find("opponentInput")->property("currentText").toString() != QLatin1StringView("Lech")) {
        fail("the field does not show the new opponent");
    }
    find("matchDateInput")->forceActiveFocus(Qt::TabFocusReason);
    retypeDate(window, find("matchDateInput"), QDate(2030, 1, 12));
    find("kickoffInput")->forceActiveFocus(Qt::TabFocusReason);
    typeText(window, QStringLiteral("1830"));
    find("competitionInput")->forceActiveFocus(Qt::TabFocusReason);
    typeText(window, QStringLiteral("Liga"));
    capture("calendar-match-editor");
    pressKey(window, Qt::Key_Return);
    expectFlag(matchDialog, "visible", false, "Enter did not save the match");

    // Done when: a session from the keyboard.
    chooseFromShortcut(true);
    expectFlag(sessionDialog, "visible", true, "the choice did not open the session form");
    retypeDate(window, find("sessionDateInput"), QDate(2030, 1, 8));
    capture("calendar-session-editor");
    pressKey(window, Qt::Key_Return);
    expectFlag(sessionDialog, "visible", false, "Enter did not save the session");

    // Done when: both on the right days, in both modes.
    if (calendar->items()->rowCount() != 2) {
        fail("the keyboard did not add a match and a session");
        return;
    }
    if (calendar->rowAt(0).value(QStringLiteral("date")).toString() !=
            QLatin1StringView("2030-01-08") ||
        calendar->rowAt(1).value(QStringLiteral("date")).toString() !=
            QLatin1StringView("2030-01-12") ||
        calendar->rowAt(1).value(QStringLiteral("time")).toString() != QLatin1StringView("18:30")) {
        fail("the list does not show the items on their days");
    }
    capture("calendar-list");
    if (calendar->weekStart() != QLatin1StringView("2030-01-07")) {
        fail("a save did not move to the week of the saved item");
    }
    if (itemsOfDay(calendar, 1).size() != 1 || itemsOfDay(calendar, 5).size() != 1) {
        fail("the week does not show the items on their days");
    }
    const QVariantMap matchRow = calendar->rowAt(1);
    const QVariantMap sessionRow = calendar->rowAt(0);
    const qint64 matchId = matchRow.value(QStringLiteral("itemId")).toLongLong();
    const qint64 sessionId = sessionRow.value(QStringLiteral("itemId")).toLongLong();
    if (!expectFollowsLanguage(engine, [calendar] {
            return calendar->rowAt(0).value(QStringLiteral("dateText")).toString();
        })) {
        fail("the date in the calendar list stayed in the old language");
    }

    // Enter on the selected row opens it, and the sheet of a saved session is one button away.
    pressKey(window, Qt::Key_Return);
    expectFlag(sessionDialog, "visible", true, "Enter did not open the selected session");
    auto* openSheet = find("openSheetButton");
    expectFlag(openSheet, "enabled", true, "a saved session has no way to its sheet");
    sessionEditor->setNote(QStringLiteral("Zmiana"));
    settle();
    expectFlag(openSheet, "enabled", false,
               "unsaved changes would be lost on the way to the sheet");
    sessionEditor->setNote(QString{});
    settle();
    QMetaObject::invokeMethod(openSheet, "click");
    settle();
    expectFlag(sessionDialog, "visible", false, "the sheet opened under the form");
    if (navigation->currentKey() != QLatin1StringView("training") ||
        navigation->currentArgs().value(QStringLiteral("sessionId")).toLongLong() != sessionId) {
        fail("Open training sheet did not pass the session on");
    }
    navigation->activate(QStringLiteral("calendar"));
    settle();

    // Text a field refused is not in the viewmodel, so `dirty` cannot see it: Save and the way to
    // the sheet both stay off, or it would be lost without a word.
    const auto openSession = [&] {
        QMetaObject::invokeMethod(view, "openItem", Q_ARG(QVariant, QStringLiteral("session")),
                                  Q_ARG(QVariant, double(sessionId)));
        settle();
        expectFlag(openSheet, "enabled", true, "an untouched session has no way to its sheet");
    };
    const auto expectSheetOffAndLeave = [&](const char* reason) {
        expectFlag(openSheet, "enabled", false, reason);
        expectFlag(find("saveFirstHint"), "visible", true, "a refused text gives no hint");
        expectFlag(sessionDialog, "primaryEnabled", false, "Save stays on over a refused text");
        pressKey(window, Qt::Key_Escape);
        expectFlag(sessionDialog, "visible", false, "Esc did not close the session form");
    };
    openSession();
    pressKey(window, Qt::Key_Backspace);
    expectSheetOffAndLeave("the sheet stays on over a date that is not a date");
    openSession();
    find("startTimeInput")->forceActiveFocus(Qt::TabFocusReason);
    typeText(window, QStringLiteral("1"));
    expectSheetOffAndLeave("the sheet stays on over an unfinished time");
    openSession();
    find("durationInput")->forceActiveFocus(Qt::TabFocusReason);
    typeText(window, QStringLiteral("0"));
    expectSheetOffAndLeave("the sheet stays on over a duration out of range");
    // Leaving the form dropped what was typed.
    openSession();
    pressKey(window, Qt::Key_Escape);

    view->setProperty("mode", QStringLiteral("week"));
    capture("calendar-week");
    expectFlag(weekEmptyState, "active", false,
               "the empty state stayed over a calendar with items");
    expectFlag(weekGrid, "visible", true, "the grid did not come back with the items");
    auto* weekItem = findByObjectName(view, QStringLiteral("weekItem_match_%1").arg(matchId));
    if (weekItem == nullptr) {
        fail("the week does not show the match as a button");
        return;
    }
    QMetaObject::invokeMethod(weekItem, "click");
    settle();
    expectFlag(matchDialog, "visible", true, "the week item did not open the match");
    pressKey(window, Qt::Key_Escape);
    expectFlag(matchDialog, "visible", false, "Esc did not close the match form");
    pressKey(window, Qt::Key_Right);
    if (calendar->weekStart() != QLatin1StringView("2030-01-14")) {
        fail("the Right key did not go to the next week");
    }
    pressKey(window, Qt::Key_Left);
    if (calendar->weekStart() != QLatin1StringView("2030-01-07")) {
        fail("the Left key did not go to the previous week");
    }
    QMetaObject::invokeMethod(find("todayButton"), "click");
    settle();
    expectFlag(calendar, "showsCurrentWeek", true, "Today did not come back to the current week");
    view->setProperty("mode", QStringLiteral("list"));
    settle();

    // A suggestion from the keyboard does not save the form, and a duplicate stays in its field.
    chooseFromShortcut(false);
    QMetaObject::invokeMethod(find("showNewOpponentButton"), "click");
    settle();
    find("newOpponentInput")->forceActiveFocus(Qt::TabFocusReason);
    typeText(window, QStringLiteral("LECH"));
    pressKey(window, Qt::Key_Return);
    expectFlag(matchDialog, "newOpponentShown", true, "a duplicate opponent closed its field");
    if (matchEditor->newOpponentError().isEmpty()) {
        fail("a duplicate opponent shows no error on the field");
    }
    capture("calendar-opponent-duplicate");
    pressKey(window, Qt::Key_Escape);
    expectFlag(matchDialog, "newOpponentShown", false, "Esc did not leave the new opponent field");
    expectFlag(matchDialog, "visible", true, "Esc in the new opponent field closed the form");
    find("competitionInput")->forceActiveFocus(Qt::TabFocusReason);
    pressKey(window, Qt::Key_Down);
    pressKey(window, Qt::Key_Down);
    pressKey(window, Qt::Key_Return);
    expectFlag(matchDialog, "visible", true, "picking a suggestion saved the form");
    if (matchEditor->competition() != QLatin1StringView("Liga")) {
        fail("the picked suggestion did not reach the viewmodel");
    }
    // A time that is not finished must not be saved as no time.
    find("kickoffInput")->forceActiveFocus(Qt::TabFocusReason);
    typeText(window, QStringLiteral("1"));
    expectFlag(matchDialog, "primaryEnabled", false, "Save stays on over an unfinished time");
    pressKey(window, Qt::Key_Escape);
    expectFlag(matchDialog, "visible", false, "Esc did not close the match form");

    // Deleting a match goes straight to the toast, and undo brings it back.
    QMetaObject::invokeMethod(view, "openItem", Q_ARG(QVariant, QStringLiteral("match")),
                              Q_ARG(QVariant, double(matchId)));
    settle();
    QMetaObject::invokeMethod(matchDialog, "dangerClicked");
    settle();
    expectFlag(matchDialog, "visible", false, "deleting the match left its form open");
    expectFlag(toasts, "active", true, "deleting the match shows no undo toast");
    if (calendar->items()->rowCount() != 1) {
        fail("the match was not deleted");
    }
    QMetaObject::invokeMethod(toasts, "undo");
    settle();
    if (calendar->items()->rowCount() != 2) {
        fail("undo did not bring the match back");
    }

    // Decision 9: a session without attendance asks nothing.
    QMetaObject::invokeMethod(view, "openItem", Q_ARG(QVariant, QStringLiteral("session")),
                              Q_ARG(QVariant, double(sessionId)));
    settle();
    QMetaObject::invokeMethod(sessionDialog, "dangerClicked");
    settle();
    expectFlag(deleteDialog, "visible", false, "a session without attendance asked first");
    expectFlag(toasts, "active", true, "deleting the session shows no undo toast");
    QMetaObject::invokeMethod(toasts, "undo");
    settle();
    if (calendar->items()->rowCount() != 2) {
        fail("undo did not bring the session back");
        return;
    }

    // Decision 9: a session with attendance asks, and undo brings the attendance back too.
    // Undo above gave the session a new id, and it is the first row.
    const qint64 restoredSessionId =
        calendar->rowAt(0).value(QStringLiteral("itemId")).toLongLong();
    fdss::data::TrainingRepository training(database);
    const auto players =
        ServiceRegistry::instance().squad().listPlayers(fdss::core::TeamId{appState->teamId()});
    if (!players || players->empty()) {
        fail("the calendar test has no players");
        return;
    }
    fdss::core::AttendanceDraft row;
    row.playerId = players->front().player.id;
    const std::vector<fdss::core::AttendanceDraft> rows{row};
    if (!training.saveAttendance(fdss::core::TrainingSessionId{restoredSessionId}, rows)) {
        fail("the calendar test cannot save attendance");
        return;
    }
    QMetaObject::invokeMethod(view, "openItem", Q_ARG(QVariant, QStringLiteral("session")),
                              Q_ARG(QVariant, double(restoredSessionId)));
    settle();
    QMetaObject::invokeMethod(sessionDialog, "dangerClicked");
    settle();
    expectFlag(deleteDialog, "visible", true, "a session with attendance did not ask first");
    expectFlag(sessionDialog, "visible", true, "the question closed the form under it");
    capture("calendar-delete-question");
    pressKey(window, Qt::Key_Escape);
    expectFlag(deleteDialog, "visible", false, "Esc did not close the question");
    if (calendar->items()->rowCount() != 2) {
        fail("cancelling the question deleted the session");
    }
    QMetaObject::invokeMethod(sessionDialog, "dangerClicked");
    settle();
    QMetaObject::invokeMethod(deleteDialog, "dangerClicked");
    settle();
    expectFlag(deleteDialog, "visible", false, "the question stayed after the delete");
    expectFlag(sessionDialog, "visible", false, "the form stayed after the delete");
    expectFlag(toasts, "active", true, "the delete after the question shows no undo toast");
    if (calendar->items()->rowCount() != 1) {
        fail("the question did not lead to the delete");
    }
    QMetaObject::invokeMethod(toasts, "undo");
    settle();
    const qint64 backAgainId = calendar->rowAt(0).value(QStringLiteral("itemId")).toLongLong();
    if (calendar->attendanceCount(backAgainId) != 1) {
        fail("undo did not bring the attendance back");
    }

    // A match that is not planned opens read-only, without a way to change it.
    fdss::data::MatchRepository matches(database);
    const qint64 currentMatchId = calendar->rowAt(1).value(QStringLiteral("itemId")).toLongLong();
    if (!matches.setResult(fdss::core::MatchId{currentMatchId}, 2, 1,
                           fdss::core::MatchStatus::Finished)) {
        fail("the calendar test cannot finish the match");
        return;
    }
    calendar->reload();
    settle();
    // A finished match opens its report (P14), not the form.
    QMetaObject::invokeMethod(view, "openItem", Q_ARG(QVariant, QStringLiteral("match")),
                              Q_ARG(QVariant, double(currentMatchId)));
    settle();
    capture("calendar-match-finished");
    expectFlag(matchDialog, "visible", false, "a finished match opened the form");
    if (navigation->currentKey() != QLatin1StringView("matchreport")) {
        fail("a finished match does not open its report from the calendar");
    }
    if (auto* reportView = findQmlItem(window->contentItem(), "MatchReportView")) {
        auto* report = reportView->property("viewModel").value<MatchReportViewModel*>();
        if (report == nullptr || report->matchId() != currentMatchId || !report->hasReport()) {
            fail("the report of the finished match is not shown");
        }
        // Back leads to the calendar.
        clickItem(window, findByObjectName(reportView, QStringLiteral("reportBackButton")));
        if (navigation->currentKey() != QLatin1StringView("calendar")) {
            fail("Back of the report does not lead to the calendar");
        }
    } else {
        fail("the match report view is not loaded");
    }
    navigation->activate(QStringLiteral("calendar"));
    settle();

    // A role without EditCalendar reads the calendar and reaches the training sheet.
    auto& auth = ServiceRegistry::instance().auth();
    const fdss::core::StaffUserId coach = ServiceRegistry::instance().session().user()->id;
    fdss::core::StaffUserDraft physioDraft;
    physioDraft.name = "Piotr Fizjo";
    physioDraft.role = fdss::core::Role::Physio;
    const auto physio = auth.addStaff(physioDraft, "1234");
    if (!physio || !auth.login(*physio, "1234")) {
        fail("the physio cannot log in");
        return;
    }
    appState->refresh();
    settle();
    navigation->activate(QStringLiteral("calendar"));
    settle();
    auto* physioView = findQmlItem(window->contentItem(), "CalendarView");
    if (physioView == nullptr) {
        fail("the physio has no calendar");
        return;
    }
    auto* physioCalendar = physioView->property("viewModel").value<CalendarViewModel*>();
    auto* physioSession = physioView->property("sessionEditor").value<QObject*>();
    expectFlag(physioView, "canEdit", false, "the physio may edit the calendar");
    expectFlag(physioView->findChild<QObject*>(QStringLiteral("addShortcut")), "enabled", false,
               "the physio has Ctrl+N in the calendar");
    expectFlag(find("addMatchButton"), "visible", false, "the physio sees Add match");
    if (physioCalendar->items()->rowCount() != 2) {
        fail("the physio does not see the calendar");
    }
    QMetaObject::invokeMethod(physioView, "openItem", Q_ARG(QVariant, QStringLiteral("session")),
                              Q_ARG(QVariant, double(backAgainId)));
    capture("calendar-session-read-only");
    if (!physioSession->property("primaryText").toString().isEmpty() ||
        !physioSession->property("dangerText").toString().isEmpty()) {
        fail("the physio can still save or delete a session");
    }
    expectFlag(find("openSheetButton"), "enabled", true, "the physio has no way to the sheet");
    pressKey(window, Qt::Key_Escape);

    // An empty calendar in the week: the text alone, as this role has no next step of its own.
    if (!training.remove(fdss::core::TrainingSessionId{backAgainId}) ||
        !matches.setResult(fdss::core::MatchId{currentMatchId}, std::nullopt, std::nullopt,
                           fdss::core::MatchStatus::Planned) ||
        !matches.removePlanned(fdss::core::MatchId{currentMatchId})) {
        fail("the calendar test cannot empty the calendar");
        return;
    }
    physioCalendar->reload();
    physioView->setProperty("mode", QStringLiteral("week"));
    capture("calendar-week-empty-read-only");
    auto* physioEmptyState = findByObjectName(physioView, QStringLiteral("weekEmptyState"));
    expectFlag(physioEmptyState, "active", true, "the physio has no empty state in the week");
    const auto* physioEmpty = physioEmptyState->property("item").value<QObject*>();
    if (!physioEmpty->property("actionText").toString().isEmpty() ||
        physioEmpty->property("description").toString().isEmpty()) {
        fail("the empty week of the physio should be text without a button");
    }

    if (!auth.login(coach, "1234")) {
        fail("the coach cannot log in again");
        return;
    }
    appState->refresh();
    settle();
    navigation->setCurrentIndex(0);
    settle();
}

SetupWizardViewModel* wizardOf(QQuickWindow* window) {
    auto* view = window->property("setupWizard").value<QQuickItem*>();
    return view != nullptr ? view->property("viewModel").value<SetupWizardViewModel*>() : nullptr;
}

ProfilePickerViewModel* pickerOf(QQuickWindow* window) {
    auto* view = window->property("profilePicker").value<QQuickItem*>();
    return view != nullptr ? view->property("viewModel").value<ProfilePickerViewModel*>() : nullptr;
}

// The theme toggle is in the corner on every step, the language buttons from the second step on:
// the first step has a language control of its own.
void expectWizardCorner(QQuickWindow* window, bool languageButtons) {
    auto* view = window->property("setupWizard").value<QQuickItem*>();
    auto* corner =
        view != nullptr ? view->findChild<QQuickItem*>(QStringLiteral("cornerSwitch")) : nullptr;
    if (corner == nullptr) {
        fail("the wizard has no corner switch");
        return;
    }
    auto* toggle = corner->findChild<QQuickItem*>(QStringLiteral("themeToggle"));
    auto* buttons = corner->findChild<QQuickItem*>(QStringLiteral("languageButtons"));
    if (toggle == nullptr || buttons == nullptr) {
        fail("the corner switch of the wizard is not all there");
        return;
    }
    if (!toggle->isVisible()) {
        fail("the wizard does not offer the theme toggle");
    }
    if (buttons->isVisible() != languageButtons) {
        fail(languageButtons ? "the language buttons are not there after the first step"
                             : "the first step of the wizard shows two language controls");
    }
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

    // The loop of the caller brings this to both themes and both languages.
    expectWizardCorner(window, false);
    capture(QStringLiteral("1-welcome"));
    wizard->next();
    settle();
    expectWizardCorner(window, true);
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
    wizard->setLoadDemoData(true);
    capture(QStringLiteral("4-summary-demo"));
    wizard->setLoadDemoData(false);

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
    if (!expectFollowsLanguage(engine, [wizard] { return wizard->clubNameError(); })) {
        fail("an error of the wizard stayed in the old language");
    }
    expectWizardCorner(window, true);
    wizard->back();
    settle();
    expectWizardCorner(window, false);

    // The theme toggle works on the first step, to the other theme and back.
    auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                            QStringLiteral("AppSettings"));
    auto* themeToggle = window->property("setupWizard")
                            .value<QQuickItem*>()
                            ->findChild<QQuickItem*>(QStringLiteral("themeToggle"));
    if (themeToggle == nullptr) {
        fail("the first step of the wizard has no theme toggle");
        return;
    }
    const bool startedDark = settings->darkTheme();
    for (const bool expectedDark : {!startedDark, startedDark}) {
        QMetaObject::invokeMethod(themeToggle, "click");
        settle();
        window->grabWindow();
        if (settings->darkTheme() != expectedDark) {
            fail("the theme toggle on the first step of the wizard did not switch the theme");
        }
    }

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

    // In the Change PIN dialog the controls follow the lockout, and come back when its time is up.
    auto* changePin = topBar->findChild<ChangePinViewModel*>();
    auto* changePinDialog = topBar->findChild<QObject*>(QStringLiteral("changePinDialog"));
    auto* currentPinInput = topBar->findChild<QQuickItem*>(QStringLiteral("currentPinInput"));
    if (changePin == nullptr || changePinDialog == nullptr || currentPinInput == nullptr) {
        fail("the Change PIN dialog is not all there");
        return;
    }
    QMetaObject::invokeMethod(topBar, "openChangePinDialog");
    settle();
    for (int attempt = 0; attempt < 5; ++attempt) {
        changePin->change(QStringLiteral("9999"), QStringLiteral("5678"), QStringLiteral("5678"));
    }
    settle();
    window->grabWindow();
    if (!changePin->locked() || currentPinInput->isEnabled() ||
        changePinDialog->property("primaryEnabled").toBool()) {
        fail("the Change PIN dialog can still be used during a lockout");
    }
    g_clock->advance(fdss::services::kLockoutDuration);
    changePin->tickLock();
    settle();
    if (changePin->locked() || !currentPinInput->isEnabled() ||
        !changePinDialog->property("primaryEnabled").toBool()) {
        fail("the Change PIN dialog did not come back after the lockout");
    }
    pressKey(window, Qt::Key_Escape);

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
    if (!expectFollowsLanguage(engine, [picker] { return picker->message(); })) {
        fail("the message of the picker stayed in the old language");
    }
    if (window->property("profilePicker")
            .value<QQuickItem*>()
            ->findChild<QQuickItem*>(QStringLiteral("cornerSwitch")) == nullptr) {
        fail("the picker has no language and theme switch");
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

    // The controls follow the lockout, and come back when its time is up.
    auto* pickerView = window->property("profilePicker").value<QQuickItem*>();
    auto* pinField = pickerView->findChild<QQuickItem*>(QStringLiteral("pinInput"));
    auto* loginButton = pickerView->findChild<QQuickItem*>(QStringLiteral("loginButton"));
    if (pinField == nullptr || loginButton == nullptr) {
        fail("the PIN field or the login button is not there");
        return;
    }
    if (pinField->isEnabled() || loginButton->isEnabled()) {
        fail("a locked profile can still enter a PIN");
    }
    g_clock->advance(fdss::services::kLockoutDuration);
    picker->tickLock();
    settle();
    if (!pinField->isEnabled() || !loginButton->isEnabled()) {
        fail("the PIN field and the login button did not come back after the lockout");
    }

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

// With more tiles than fit, a tile that gets the focus by Tab scrolls into view, down and, when Tab
// wraps around, up again. Expects a loaded Main.qml in the state NeedsLogin with the Coach of
// installRegistry().
void exerciseTileScrolling(QQmlApplicationEngine& engine) {
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* picker = pickerOf(window);
    if (picker == nullptr) {
        fail("the picker is not there");
        return;
    }
    auto& auth = ServiceRegistry::instance().auth();
    const qint64 coachId = picker->staff()->staff().front().id.value;
    if (const auto login = auth.login(fdss::core::StaffUserId{coachId}, "1234");
        !login || login->status != fdss::services::PinCheckStatus::Accepted) {
        fail("the coach cannot log in to add the staff");
        return;
    }
    constexpr int kAddedStaff = 24;
    for (int i = 1; i <= kAddedStaff; ++i) {
        fdss::core::StaffUserDraft draft;
        draft.name = QStringLiteral("Osoba %1").arg(i).toStdString();
        if (!auth.addStaff(draft, "1234")) {
            fail("cannot add the staff");
            return;
        }
    }
    auth.logout();
    picker->reload();
    settle();
    // The Flow places the new tiles, and the layout sizes the area, when the window is polished on
    // the way to a frame. processEvents() alone reaches that frame only sometimes: whether the
    // tiles "fit" depended on the timing of the render loop.
    window->grabWindow();

    auto* view = window->property("profilePicker").value<QQuickItem*>();
    auto* area = view->findChild<QQuickItem*>(QStringLiteral("tilesArea"));
    if (area == nullptr) {
        fail("the area of the tiles is not there");
        return;
    }
    if (area->property("contentHeight").toReal() <= area->height()) {
        fail("the tiles fit, so this proves nothing");
        return;
    }

    QSet<QQuickItem*> focusedTiles;
    bool scrolledDown = false;
    bool scrolledUp = false;
    qreal lastContentY = area->property("contentY").toReal();
    constexpr int kTabPresses = 3 * kAddedStaff; // more than one way around
    for (int press = 0; press < kTabPresses; ++press) {
        pressKey(window, Qt::Key_Tab);
        QQuickItem* focused = window->activeFocusItem();
        if (focused == nullptr || !QLatin1StringView(focused->metaObject()->className())
                                       .startsWith(QLatin1StringView("ProfileTile"))) {
            continue;
        }
        focusedTiles.insert(focused);
        const QRectF rect = focused->mapRectToItem(area, focused->boundingRect());
        if (rect.top() < 0 || rect.bottom() > area->height()) {
            fail("Tab reached a tile outside of the visible area");
        }
        const qreal contentY = area->property("contentY").toReal();
        scrolledDown = scrolledDown || contentY > lastContentY;
        scrolledUp = scrolledUp || contentY < lastContentY;
        lastContentY = contentY;
    }
    if (focusedTiles.size() != kAddedStaff + 1) {
        fail("Tab did not reach every tile");
    }
    if (!scrolledDown || !scrolledUp) {
        fail("Tab did not scroll the tiles both ways");
    }
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

// Every item under `item` whose text holds the markup. Each one that can render markup must have
// Text.PlainText; a TextInput has no textFormat and never renders it.
int countPlainMarkup(QQuickItem* item, const QString& markup, const char* where) {
    int found = 0;
    if (item->property("text").toString().contains(markup)) {
        const QVariant format = item->property("textFormat");
        if (format.isValid() && format.toInt() != Qt::PlainText) {
            std::fprintf(stderr, "qml_smoke: %s: rich text in %s\n", where,
                         item->metaObject()->className());
            fail("markup in user data is not shown as plain text");
        }
        ++found;
    }
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        found += countPlainMarkup(child, markup, where);
    }
    return found;
}

// User data with markup, from the wizard to the profile picker. Not one piece may render as rich
// text: that would show something else than what was typed, and <img> would go to the network
// (AGENTS sections 1 and 6). Expects an empty database and a loaded Main.qml.
void exerciseUserMarkup(QQmlApplicationEngine& engine) {
    const QString markup = QStringLiteral("<b>x</b><img src=\"http://example.invalid/a.png\">");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    auto* toasts =
        engine.singletonInstance<QObject*>(QStringLiteral("FootballDSS"), QStringLiteral("Toasts"));
    const auto expectShown = [&](const char* where) {
        settle();
        window->grabWindow();
        if (countPlainMarkup(window->contentItem(), markup, where) == 0) {
            std::fprintf(stderr, "qml_smoke: %s\n", where);
            fail("the markup is not on the screen where it should be");
        }
    };

    auto* wizard = wizardOf(window);
    if (wizard == nullptr) {
        fail("the wizard is not there for the markup test");
        return;
    }
    wizard->next();
    wizard->setClubName(markup);
    wizard->setTeamName(markup);
    wizard->next();
    wizard->setCoachName(markup);
    wizard->setPin(QStringLiteral("1234"));
    wizard->setPinRepeat(QStringLiteral("1234"));
    wizard->next();
    expectShown("wizard summary");
    if (!wizard->create()) {
        fail("the wizard did not take names with markup");
        return;
    }
    expectShown("top bar");

    fdss::core::PlayerDraft draft;
    draft.firstName = markup.toStdString();
    draft.lastName = "Nowak";
    draft.notes = markup.toStdString();
    auto& squadService = ServiceRegistry::instance().squad();
    const auto playerId = squadService.addPlayer(
        fdss::core::TeamId{static_cast<std::int64_t>(appState->teamId())}, draft);
    if (!playerId) {
        fail("the player with markup was not added");
        return;
    }
    navigation->activate(QStringLiteral("squad"));
    expectShown("squad table");

    auto* view = findQmlItem(window->contentItem(), "SquadView");
    auto* card = findQmlItem(window->contentItem(), "PlayerCardView");
    view->findChild<QObject*>(QStringLiteral("playersTable"))->setProperty("currentIndex", 0);
    settle();
    QMetaObject::invokeMethod(view, "archiveSelected");
    expectShown("undo toast");
    QMetaObject::invokeMethod(toasts, "undo");
    settle();

    navigation->activate(QStringLiteral("squad"),
                         QVariantMap{{QStringLiteral("playerId"), qint64{playerId->value}}});
    auto* availability = card->property("availabilityModel").value<AvailabilityViewModel*>();
    if (!availability->add(QStringLiteral("injury"), QStringLiteral("2026-10-01"), {}, markup)) {
        fail("the description with markup was not added");
    }
    card->findChild<QObject*>(QStringLiteral("entriesTable"))->setProperty("currentIndex", 0);
    expectShown("player card");
    if (const auto* notes = findByObjectName(card, QStringLiteral("playerData_notes"));
        notes == nullptr || notes->property("text").toString() != markup) {
        fail("the notes do not show what was typed");
    }
    QMetaObject::invokeMethod(card, "openEditAvailability");
    expectShown("availability dialog");
    pressKey(window, Qt::Key_Escape);
    QMetaObject::invokeMethod(view, "closeCard");
    settle();

    navigation->activate(QStringLiteral("settings"));
    expectShown("staff table");
    auto* settingsView = findQmlItem(window->contentItem(), "SettingsView");
    auto* staffSection = settingsView->property("staffSection").value<QQuickItem*>();
    QMetaObject::invokeMethod(staffSection, "openResetPinDialog");
    expectShown("reset PIN dialog");
    pressKey(window, Qt::Key_Escape);

    appState->logout();
    expectShown("profile picker");
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

// Every wait below ends, with the condition or with the reason: nothing may hang on a Windows
// runner.
template <class Condition> bool waitUntil(Condition condition, const char* reason) {
    constexpr qint64 kLimitMs = 5000;
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > kLimitMs) {
            fail(reason);
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    return true;
}

fdss::core::TeamId smokeTeam() {
    return ServiceRegistry::instance().setup().clubContext()->team.id;
}

// A session on the day of the FakeClock, and players numbered from 1, in rows from 0. Row 2 is
// injured and row 4 away on that day; row 6 is suspended, which keeps nobody from training.
constexpr fdss::core::Date kSheetDay{std::chrono::year{2026}, std::chrono::November,
                                     std::chrono::day{1}};
constexpr int kInjuredRow = 2;
constexpr int kAwayRow = 4;
constexpr int kSuspendedRow = 6;

std::optional<fdss::core::TrainingSessionId> seedSheet(int playerCount) {
    auto& registry = ServiceRegistry::instance();
    fdss::core::TrainingSessionDraft session;
    session.date = kSheetDay;
    const auto sessionId = registry.calendar().addSession(smokeTeam(), session);
    if (!sessionId) {
        fail("the training test cannot add a session");
        return std::nullopt;
    }
    for (int row = 0; row < playerCount; ++row) {
        fdss::core::PlayerDraft draft;
        draft.firstName = "Zawodnik";
        draft.lastName = QStringLiteral("Numer %1").arg(row + 1).toStdString();
        draft.shirtNumber = row + 1;
        const auto player = registry.squad().addPlayer(smokeTeam(), draft);
        if (!player) {
            fail("the training test cannot add a player");
            return std::nullopt;
        }
        fdss::core::AvailabilityDraft entry;
        entry.fromDate = kSheetDay;
        entry.expectedTo = kSheetDay;
        if (row == kInjuredRow || row == kAwayRow || row == kSuspendedRow) {
            entry.type = row == kInjuredRow ? fdss::core::AvailabilityType::Injury
                         : row == kAwayRow  ? fdss::core::AvailabilityType::Absence
                                            : fdss::core::AvailabilityType::Suspension;
            if (!registry.squad().addAvailability(*player, entry)) {
                fail("the training test cannot add an availability entry");
                return std::nullopt;
            }
        }
    }
    return *sessionId;
}

struct TrainingScreen {
    QQuickWindow* window = nullptr;
    NavigationModel* navigation = nullptr;
    QQuickItem* view = nullptr;
    TrainingSessionViewModel* sheet = nullptr;
    QObject* question = nullptr;

    [[nodiscard]] QQuickItem* find(const QString& name) const {
        return findByObjectName(window->contentItem(), name);
    }
    [[nodiscard]] QQuickItem* cell(const char* kind, int row) const {
        return find(QStringLiteral("%1_%2").arg(QString::fromLatin1(kind)).arg(row));
    }
    [[nodiscard]] bool focused(const char* kind, int row) const {
        const QQuickItem* item = cell(kind, row);
        return item != nullptr && item->hasActiveFocus();
    }
    [[nodiscard]] QVariant value(int row, TrainingSessionViewModel::DataRole role) const {
        return sheet->index(row).data(role);
    }
};

// Goes to the section, and finds it anew: a login builds the views again.
std::optional<TrainingScreen> openTrainingScreen(QQmlApplicationEngine& engine) {
    TrainingScreen screen;
    screen.window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    screen.navigation = screen.window->property("navigation").value<NavigationModel*>();
    screen.navigation->activate(QStringLiteral("training"));
    settle();
    screen.view = findQmlItem(screen.window->contentItem(), "TrainingView");
    if (screen.view == nullptr) {
        fail("the training view is not loaded");
        return std::nullopt;
    }
    screen.sheet = screen.view->property("viewModel").value<TrainingSessionViewModel*>();
    screen.question = screen.view->property("leaveDialog").value<QObject*>();
    if (screen.sheet == nullptr || screen.question == nullptr) {
        fail("the training view is not all there");
        return std::nullopt;
    }
    return screen;
}

bool logInAgain(QQmlApplicationEngine& engine, fdss::core::StaffUserId user) {
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    if (!ServiceRegistry::instance().auth().login(user, "1234")) {
        fail("the training test cannot log in");
        return false;
    }
    appState->refresh();
    settle();
    return true;
}

std::optional<fdss::core::StaffUserId> addSmokeStaff(const char* name, fdss::core::Role role) {
    fdss::core::StaffUserDraft draft;
    draft.name = name;
    draft.role = role;
    const auto user = ServiceRegistry::instance().auth().addStaff(draft, "1234");
    if (!user) {
        fail("the training test cannot add a staff member");
        return std::nullopt;
    }
    return *user;
}

void pressDigit(QQuickWindow* window, int digit) {
    pressKey(window, static_cast<Qt::Key>(Qt::Key_0 + digit), QString::number(digit));
}

// The way of the empty states: what comes next depends on what the role may do.
void exerciseTrainingEmptyStates(QQmlApplicationEngine& engine) {
    const fdss::core::StaffUserId coach = ServiceRegistry::instance().session().user()->id;
    auto screen = openTrainingScreen(engine);
    if (!screen) {
        return;
    }
    auto* noSessions = screen->find(QStringLiteral("noSessionsState"));
    if (noSessions == nullptr) {
        fail("the training view has no empty state");
        return;
    }
    expectFlag(noSessions, "visible", true, "no sessions, and no empty state");
    if (noSessions->property("actionText").toString().isEmpty()) {
        fail("the empty sheet has no button to the calendar for the coach");
    }
    QMetaObject::invokeMethod(noSessions, "actionTriggered");
    settle();
    if (screen->navigation->currentKey() != QLatin1StringView("calendar")) {
        fail("the button of the empty sheet does not lead to the calendar");
    }

    const auto physio = addSmokeStaff("Piotr Fizjo", fdss::core::Role::Physio);
    if (!physio || !logInAgain(engine, *physio)) {
        return;
    }
    screen = openTrainingScreen(engine);
    if (!screen) {
        return;
    }
    noSessions = screen->find(QStringLiteral("noSessionsState"));
    expectFlag(noSessions, "visible", true, "no sessions, and no empty state for the physio");
    if (!noSessions->property("actionText").toString().isEmpty()) {
        fail("the physio is offered a button to a calendar they cannot add to");
    }
    logInAgain(engine, coach);
}

// Done when: 20 players from the keyboard only, saved, with the same values after reading them
// again.
void exerciseTrainingKeyboard(TrainingScreen& screen, fdss::core::TrainingSessionId session,
                              const QString& screenshotDirectory) {
    constexpr int kPlayers = 20;
    QQuickWindow* window = screen.window;
    TrainingSessionViewModel* sheet = screen.sheet;
    using VM = TrainingSessionViewModel;
    const auto capture = [&](const char* name) {
        settle();
        grabOrSave(window, screenshotDirectory, QString::fromLatin1(name) + QStringLiteral(".png"));
    };

    if (sheet->count() != kPlayers) {
        fail("the sheet does not show the squad");
        return;
    }
    if (screen.value(kInjuredRow, VM::StatusRole).toString() != QLatin1StringView("injured") ||
        screen.value(kAwayRow, VM::StatusRole).toString() != QLatin1StringView("excused") ||
        screen.value(kSuspendedRow, VM::StatusRole).toString() != QLatin1StringView("present")) {
        fail("the availability did not decide the default statuses");
    }
    auto* saveButton = screen.find(QStringLiteral("saveButton"));
    auto* saveShortcut = screen.view->findChild<QObject*>(QStringLiteral("saveShortcut"));
    if (saveButton == nullptr || saveShortcut == nullptr) {
        fail("the sheet has no way to save");
        return;
    }
    expectFlag(saveButton, "enabled", true, "a sheet never saved cannot be saved");
    auto* saveState = screen.find(QStringLiteral("saveStateBadge"));
    if (saveState == nullptr) {
        fail("the sheet does not say whether it is saved");
        return;
    }
    if (saveState->property("text").toString() != sheet->unsavedText() ||
        sheet->unsavedCount() != 20) {
        fail("a sheet never saved does not say how many players are not saved");
    }
    expectFlag(sheet, "dirty", false, "opening a sheet counts as a change");
    if (!waitUntil([&] { return screen.focused("rpeCell", 0); },
                   "the first RPE has no focus when the sheet opens")) {
        return;
    }
    capture("training-sheet");

    for (int row = 0; row < kPlayers; ++row) {
        if (row == kInjuredRow || row == kAwayRow) {
            continue;
        }
        if (!screen.focused("rpeCell", row)) {
            fail("Enter did not lead to the next enabled RPE");
            return;
        }
        pressDigit(window, 2 + (row % 8));
        pressKey(window, Qt::Key_Return);
    }
    if (!screen.focused("rpeCell", kPlayers - 1)) {
        fail("Enter on the last row moved the focus away");
    }
    expectFlag(saveShortcut, "enabled", true, "Ctrl+S is off over changes");
    QMetaObject::invokeMethod(saveShortcut, "activated");
    settle();
    expectFlag(sheet, "dirty", false, "the save left changes behind");
    expectFlag(saveButton, "enabled", false, "Save stays on with nothing changed");
    expectFlag(saveShortcut, "enabled", false, "Ctrl+S stays on with nothing changed");
    if (saveState->property("text").toString() == sheet->unsavedText() ||
        sheet->unsavedCount() != 0) {
        fail("a save does not say that the sheet is saved");
    }

    // A player who comes later is not saved, and the sheet says so; nothing asks before leaving.
    fdss::core::PlayerDraft late;
    late.firstName = "Zawodnik";
    late.lastName = "Spozniony";
    late.shirtNumber = kPlayers + 1;
    const auto latePlayer = ServiceRegistry::instance().squad().addPlayer(smokeTeam(), late);
    if (!latePlayer) {
        fail("the training test cannot add a late player");
        return;
    }
    sheet->reload();
    settle();
    expectFlag(saveButton, "enabled", true, "a player not saved cannot be saved");
    expectFlag(sheet, "dirty", false, "a player not saved counts as a change");
    if (saveState->property("text").toString() != sheet->unsavedText() ||
        sheet->unsavedCount() != 1) {
        fail("the sheet does not say that a player is not saved");
    }
    capture("training-sheet-partly-saved");
    QMetaObject::invokeMethod(saveShortcut, "activated");
    settle();
    if (sheet->unsavedCount() != 0) {
        fail("the save left a player unsaved");
    }
    if (!ServiceRegistry::instance().squad().archivePlayer(*latePlayer)) {
        fail("the training test cannot archive the late player");
        return;
    }
    sheet->reload();
    settle();
    screen.cell("rpeCell", kPlayers - 1)->forceActiveFocus(Qt::TabFocusReason);
    settle();
    if (!screen.focused("rpeCell", kPlayers - 1)) {
        fail("the save took the focus out of the grid");
    }
    capture("training-sheet-saved");

    // Read again, straight from the service and through the viewmodel.
    const auto stored = ServiceRegistry::instance().training().sheet(session);
    if (!stored || std::ssize(stored->rows) != kPlayers) {
        fail("the saved sheet cannot be read");
        return;
    }
    sheet->reload();
    settle();
    for (int row = 0; row < kPlayers; ++row) {
        const bool out = row == kInjuredRow || row == kAwayRow;
        const QVariant rpe = screen.value(row, VM::RpeRole);
        if (out ? rpe.isValid() : rpe.toInt() != 2 + (row % 8)) {
            fail("a saved RPE did not come back");
        }
    }

    // Tab goes through the columns and past what is disabled; Esc takes back what was typed.
    screen.cell("rpeCell", 0)->forceActiveFocus(Qt::TabFocusReason);
    pressKey(window, Qt::Key_Tab);
    if (!screen.focused("minutesCell", 0)) {
        fail("Tab did not lead from RPE to the minutes");
    }
    typeText(window, QStringLiteral("60"));
    if (screen.value(0, VM::LoadRole).toInt() != screen.value(0, VM::RpeRole).toInt() * 60) {
        fail("typed minutes did not change the load");
    }
    pressKey(window, Qt::Key_Escape);
    if (screen.value(0, VM::MinutesRole).toInt() != sheet->durationMin() ||
        screen.cell("minutesCell", 0)->property("displayText").toString() !=
            QString::number(sheet->durationMin())) {
        fail("Esc did not take the minutes back");
    }
    expectFlag(sheet, "dirty", false, "Esc left a change behind");
    pressKey(window, Qt::Key_Return);
    if (!screen.focused("minutesCell", 1)) {
        fail("Enter did not lead to the minutes of the next row");
    }
    pressKey(window, Qt::Key_Tab);
    if (!screen.focused("statusCell", kInjuredRow)) {
        fail("Tab did not lead from the minutes to the status of the next row");
    }
    pressKey(window, Qt::Key_Tab);
    if (!screen.focused("statusCell", kInjuredRow + 1)) {
        fail("Tab did not skip the disabled cells of an injured player");
    }
    const QVariant rpeBefore = screen.value(kInjuredRow + 1, VM::RpeRole);
    pressKey(window, Qt::Key_Right);
    if (screen.value(kInjuredRow + 1, VM::StatusRole).toString() != QLatin1StringView("absent") ||
        screen.value(kInjuredRow + 1, VM::RpeRole).isValid()) {
        fail("the Right key did not change the status and clear the RPE");
    }
    capture("training-sheet-absent");
    pressKey(window, Qt::Key_Escape);
    if (screen.value(kInjuredRow + 1, VM::RpeRole) != rpeBefore) {
        fail("Esc on the status did not bring the RPE back");
    }
    expectFlag(sheet, "dirty", false, "Esc on the status left a change behind");

    // Minutes out of range: the error is in the row, the save refuses, and nothing changes.
    screen.cell("minutesCell", 1)->forceActiveFocus(Qt::TabFocusReason);
    typeText(window, QStringLiteral("300"));
    expectFlag(screen.cell("rowError", 1), "visible", true, "minutes out of range show no error");
    capture("training-sheet-error");
    QMetaObject::invokeMethod(saveShortcut, "activated");
    settle();
    expectFlag(sheet, "dirty", true, "a refused save lost the changes");
    if (!screen.focused("minutesCell", 1)) {
        fail("a refused save did not lead to the row with the error");
    }
    pressKey(window, Qt::Key_Escape);
    expectFlag(sheet, "dirty", false, "Esc did not take the refused minutes back");

    // Clicked, the button takes the focus; the save brings it back to the grid.
    screen.cell("rpeCell", 7)->forceActiveFocus(Qt::TabFocusReason);
    pressDigit(window, 8);
    saveButton->forceActiveFocus(Qt::MouseFocusReason);
    QMetaObject::invokeMethod(saveButton, "click");
    settle();
    if (!screen.focused("rpeCell", 7)) {
        fail("Save did not bring the focus back to the grid");
    }

    // Mark all present leaves those whom the availability keeps out.
    sheet->setStatus(kSuspendedRow, QStringLiteral("absent"));
    QMetaObject::invokeMethod(screen.find(QStringLiteral("markAllPresentButton")), "click");
    settle();
    if (screen.value(kSuspendedRow, VM::StatusRole).toString() != QLatin1StringView("present") ||
        screen.value(kInjuredRow, VM::StatusRole).toString() != QLatin1StringView("injured") ||
        screen.value(kAwayRow, VM::StatusRole).toString() != QLatin1StringView("excused")) {
        fail("Mark all present did not follow the availability");
    }
    sheet->discardChanges();
    settle();
}

// B of the decisions: the statuses in Polish, in the smallest window, the widest word selected.
void exerciseTrainingMinimumWindow(QQmlApplicationEngine& engine, TrainingScreen& screen,
                                   const QString& screenshotDirectory) {
    constexpr int kMinimumWidth = 1280;
    constexpr int kMinimumHeight = 720;
    constexpr int kWidestStatusRow = 9;
    constexpr qreal kMinimumNameWidth = 120;
    auto* languages = engine.singletonInstance<LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));
    const QString language = languages->language();
    const QSize originalSize = screen.window->size();
    screen.window->resize(kMinimumWidth, kMinimumHeight);
    screen.sheet->setStatus(kWidestStatusRow, QStringLiteral("excused"));
    for (const char* code : {"pl", "en"}) {
        languages->setLanguage(QString::fromLatin1(code));
        settle();
        grabOrSave(screen.window, screenshotDirectory,
                   QStringLiteral("training-sheet-minimum-%1.png").arg(QString::fromLatin1(code)));
        auto* grid = screen.find(QStringLiteral("attendanceGrid"));
        for (const int row : {0, kInjuredRow, kAwayRow, kWidestStatusRow}) {
            auto* status = screen.cell("statusCell", row);
            auto* rpe = screen.cell("rpeCell", row);
            auto* load = screen.cell("loadCell", row);
            const qreal statusRight = status->mapToItem(grid, QPointF(status->width(), 0)).x();
            const qreal rpeLeft = rpe->mapToItem(grid, QPointF(0, 0)).x();
            const qreal loadRight = load->mapToItem(grid, QPointF(load->width(), 0)).x();
            if (statusRight > rpeLeft) {
                fail("the status runs into the RPE at the minimum window");
            }
            if (loadRight > grid->width()) {
                fail("the load leaves the grid at the minimum window");
            }
        }
        // The name is what gives way, and it must stay readable.
        const qreal nameLeft = screen.cell("rpeCell", 0)->mapToItem(grid, QPointF(0, 0)).x() -
                               grid->property("statusWidth").toReal();
        if (nameLeft - grid->property("numberWidth").toReal() < kMinimumNameWidth) {
            fail("the statuses leave no room for the names at the minimum window");
        }
    }
    // Tab never leaves the focus out of sight: the last row scrolls into the small window.
    auto* lastRow = screen.cell("rpeCell", screen.sheet->count() - 1);
    lastRow->forceActiveFocus(Qt::TabFocusReason);
    settle();
    auto* page = screen.find(QStringLiteral("attendanceRows"));
    if (lastRow->mapToItem(page, QPointF(0, lastRow->height())).y() > page->height()) {
        fail("the focused last row stays out of sight");
    }
    languages->setLanguage(language);
    screen.sheet->discardChanges();
    screen.window->resize(originalSize);
    settle();
}

// C of the review: however a session is opened from the field, the first RPE gets the focus.
void exerciseTrainingSessionField(TrainingScreen& screen, fdss::core::TrainingSessionId session) {
    QQuickWindow* window = screen.window;
    TrainingSessionViewModel* sheet = screen.sheet;
    fdss::core::TrainingSessionDraft other;
    other.date = kSheetDay - std::chrono::months{2};
    const auto otherId = ServiceRegistry::instance().calendar().addSession(smokeTeam(), other);
    if (!otherId) {
        fail("the training test cannot add another session");
        return;
    }
    sheet->reload();
    settle();
    auto* field = screen.find(QStringLiteral("sessionField"));
    if (field == nullptr) {
        fail("the session field is not there");
        return;
    }
    const auto expectOpenedWithFocus = [&](qint64 sessionId, const char* wrongSession,
                                           const char* noFocus) {
        settle();
        if (sheet->sessionId() != sessionId) {
            fail(wrongSession);
            return;
        }
        waitUntil([&] { return screen.focused("rpeCell", 0); }, noFocus);
    };

    // The arrows on a closed list.
    field->forceActiveFocus(Qt::TabFocusReason);
    pressKey(window, Qt::Key_Down);
    expectOpenedWithFocus(otherId->value, "the Down key did not open the session",
                          "the Down key left the focus on the field");
    field->forceActiveFocus(Qt::TabFocusReason);
    pressKey(window, Qt::Key_Up);
    expectOpenedWithFocus(session.value, "the Up key did not open the session",
                          "the Up key left the focus on the field");

    // Enter in the open list.
    const auto popup = field->property("popup").value<QObject*>();
    field->forceActiveFocus(Qt::TabFocusReason);
    pressKey(window, Qt::Key_Space, QStringLiteral(" "));
    waitUntil([&] { return popup->property("visible").toBool(); }, "Space did not open the list");
    pressKey(window, Qt::Key_Down);
    pressKey(window, Qt::Key_Return);
    waitUntil([&] { return !popup->property("visible").toBool(); }, "Enter did not close the list");
    expectOpenedWithFocus(otherId->value, "Enter in the list did not open the session",
                          "Enter in the list left the focus on the field");

    // The mouse on an item of the open list.
    field->forceActiveFocus(Qt::TabFocusReason);
    pressKey(window, Qt::Key_Space, QStringLiteral(" "));
    waitUntil([&] { return popup->property("opened").toBool(); }, "Space did not open the list");
    settle();
    // A ListView makes its delegates when it is polished, on the way to a frame.
    window->grabWindow();
    auto* list = popup->property("contentItem").value<QQuickItem*>();
    QQuickItem* item = nullptr;
    if (list != nullptr) {
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item),
                                  Q_ARG(int, 0));
    }
    if (item == nullptr) {
        fail("the open list has no items");
        return;
    }
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = window->mapToGlobal(center);
    QMouseEvent press(QEvent::MouseButtonPress, center, center, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, center, center, global, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    waitUntil([&] { return !popup->property("visible").toBool(); },
              "a click did not close the list");
    expectOpenedWithFocus(session.value, "a click in the list did not open the session",
                          "a click in the list left the focus on the field");
}

// One question per attempt to leave, whichever way it comes.
void exerciseTrainingLeaving(QQmlApplicationEngine& engine, TrainingScreen& screen,
                             fdss::core::TrainingSessionId session) {
    QQuickWindow* window = screen.window;
    NavigationModel* navigation = screen.navigation;
    TrainingSessionViewModel* sheet = screen.sheet;
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    constexpr int kRow = 1;
    const auto change = [&](int digit) {
        sheet->enterRpeDigitAt(kRow, digit, 0);
        settle();
        expectFlag(navigation, "leaveGuard", true, "a change did not turn the guard on");
    };

    // Nothing changed: nothing asks.
    expectFlag(navigation, "leaveGuard", false, "an untouched sheet has the guard on");
    navigation->setCurrentIndex(0);
    settle();
    expectFlag(screen.question, "visible", false, "leaving an untouched sheet asked");
    if (navigation->currentKey() != QLatin1StringView("dashboard")) {
        fail("an untouched sheet could not be left");
    }
    navigation->activate(QStringLiteral("training"));
    settle();

    // The sidebar, and Stay.
    change(7);
    navigation->setCurrentIndex(0);
    settle();
    expectFlag(screen.question, "visible", true, "leaving with changes did not ask");
    if (navigation->currentKey() != QLatin1StringView("training")) {
        fail("the question did not wait for the answer");
    }
    pressKey(window, Qt::Key_Escape);
    expectFlag(screen.question, "visible", false, "Esc did not close the question");
    expectFlag(sheet, "dirty", true, "Stay lost the changes");
    if (navigation->currentKey() != QLatin1StringView("training")) {
        fail("Stay left the section");
    }

    // The keys of the sidebar move its own index first: Stay must bring it back.
    auto* sidebar = screen.find(QStringLiteral("sidebarList"));
    if (sidebar == nullptr) {
        fail("the sidebar list is not there");
        return;
    }
    const int trainingIndex = navigation->currentIndex();
    sidebar->forceActiveFocus(Qt::TabFocusReason);
    pressKey(window, Qt::Key_Down);
    expectFlag(screen.question, "visible", true, "the keys of the sidebar left without asking");
    pressKey(window, Qt::Key_Escape);
    if (sidebar->property("currentIndex").toInt() != trainingIndex ||
        navigation->currentIndex() != trainingIndex) {
        fail("Stay did not bring the sidebar back");
    }

    // The sidebar, and Discard: the move goes on, and no second question comes.
    navigation->activate(QStringLiteral("squad"));
    settle();
    QMetaObject::invokeMethod(screen.question, "dangerClicked");
    settle();
    expectFlag(screen.question, "visible", false, "Discard left the question open");
    expectFlag(sheet, "dirty", false, "Discard kept the changes");
    if (navigation->currentKey() != QLatin1StringView("squad")) {
        fail("Discard did not go on to the section asked for");
    }
    navigation->activate(QStringLiteral("training"));
    settle();

    // The sidebar, and Save.
    change(4);
    navigation->setCurrentIndex(0);
    settle();
    QMetaObject::invokeMethod(screen.question, "primaryClicked");
    settle();
    expectFlag(sheet, "dirty", false, "Save in the question left changes behind");
    if (navigation->currentKey() != QLatin1StringView("dashboard")) {
        fail("Save in the question did not go on to the section asked for");
    }
    const auto stored = ServiceRegistry::instance().training().sheet(session);
    if (!stored || !stored->rows[kRow].saved || stored->rows[kRow].draft.rpe != 4) {
        fail("Save in the question did not save");
    }
    navigation->activate(QStringLiteral("training"));
    settle();

    // A save that fails stays on the sheet with the error in the row.
    screen.cell("minutesCell", kRow)->forceActiveFocus(Qt::TabFocusReason);
    typeText(window, QStringLiteral("300"));
    navigation->setCurrentIndex(0);
    settle();
    QMetaObject::invokeMethod(screen.question, "primaryClicked");
    settle();
    expectFlag(screen.question, "visible", false, "the failed save left the question open");
    if (navigation->currentKey() != QLatin1StringView("training")) {
        fail("a failed save left the section");
    }
    expectFlag(screen.cell("rowError", kRow), "visible", true, "a failed save shows no row error");
    sheet->discardChanges();
    settle();

    // Another session in the field.
    fdss::core::TrainingSessionDraft older;
    older.date = kSheetDay - std::chrono::months{1};
    const auto olderId = ServiceRegistry::instance().calendar().addSession(smokeTeam(), older);
    if (!olderId) {
        fail("the training test cannot add another session");
        return;
    }
    sheet->reload();
    settle();
    auto* field = screen.find(QStringLiteral("sessionField"));
    const auto pickOlder = [&] {
        field->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(field, "activated", Q_ARG(int, 1));
        settle();
    };
    change(5);
    pickOlder();
    expectFlag(screen.question, "visible", true, "another session with changes did not ask");
    if (sheet->sessionId() != session.value) {
        fail("another session opened before the answer");
    }
    pressKey(window, Qt::Key_Escape);
    if (field->property("currentValue").toLongLong() != session.value) {
        fail("Stay left the field on the session that was not opened");
    }
    expectFlag(sheet, "dirty", true, "Stay in the field lost the changes");
    pickOlder();
    QMetaObject::invokeMethod(screen.question, "dangerClicked");
    settle();
    if (sheet->sessionId() != olderId->value) {
        fail("Discard did not open the session asked for");
    }
    expectFlag(navigation, "leaveGuard", false, "Discard left the guard on");
    if (navigation->currentKey() != QLatin1StringView("training")) {
        fail("another session left the section");
    }
    sheet->openSession(session.value);
    settle();

    // A logout: Stay keeps the user, Discard goes on.
    auto* topBar = findQmlItem(window->contentItem(), "TopBar");
    if (topBar == nullptr) {
        fail("the top bar is not there");
        return;
    }
    change(6);
    QMetaObject::invokeMethod(topBar, "logoutRequested");
    settle();
    expectFlag(screen.question, "visible", true, "a logout with changes did not ask");
    if (appState->state() != AppState::State::Ready) {
        fail("the logout did not wait for the answer");
    }
    pressKey(window, Qt::Key_Escape);
    if (appState->state() != AppState::State::Ready) {
        fail("Stay logged out");
    }
    QMetaObject::invokeMethod(topBar, "logoutRequested");
    settle();
    QMetaObject::invokeMethod(screen.question, "dangerClicked");
    settle();
    if (appState->state() != AppState::State::NeedsLogin) {
        fail("Discard did not go on with the logout");
    }
    expectFlag(navigation, "leaveGuard", false, "the guard survived the logout");
}

void exerciseTrainingReadOnly(QQmlApplicationEngine& engine) {
    const fdss::core::StaffUserId coach = ServiceRegistry::instance().session().user()->id;
    const auto analyst = addSmokeStaff("Anna Analityk", fdss::core::Role::Analyst);
    if (!analyst || !logInAgain(engine, *analyst)) {
        return;
    }
    auto screen = openTrainingScreen(engine);
    if (!screen) {
        return;
    }
    expectFlag(screen->sheet, "readOnly", true, "the analyst may edit the sheet");
    expectFlag(screen->find(QStringLiteral("saveButton")), "visible", false,
               "the analyst sees Save");
    expectFlag(screen->find(QStringLiteral("markAllPresentButton")), "visible", false,
               "the analyst sees Mark all present");
    expectFlag(screen->cell("statusCell", 0), "enabled", false, "the analyst can pick a status");
    expectFlag(screen->cell("rpeCell", 0), "enabled", false, "the analyst can enter an RPE");
    expectFlag(screen->view->findChild<QObject*>(QStringLiteral("saveShortcut")), "enabled", false,
               "the analyst has Ctrl+S");
    logInAgain(engine, coach);
}

void exerciseTraining(QQmlApplicationEngine& engine, const QString& screenshotDirectory) {
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        fail("Main.qml did not load");
        return;
    }
    settle();
    exerciseTrainingEmptyStates(engine);
    const auto session = seedSheet(20);
    if (!session) {
        return;
    }
    auto screen = openTrainingScreen(engine);
    if (!screen) {
        return;
    }
    // The sidebar way, on a view that was loaded before the session existed.
    if (screen->sheet->count() != 20) {
        fail("the sidebar did not bring the new session");
        return;
    }
    if (screen->cell("minutesCell", 0)->property("displayText").toString() !=
        QLatin1StringView("90")) {
        fail("the minutes cell shows something else than the duration after the sidebar");
    }
    // The way through the calendar: the sheet was left for it above.
    screen->navigation->activate(QStringLiteral("calendar"));
    settle();
    screen->navigation->activate(
        QStringLiteral("training"),
        QVariantMap{{QStringLiteral("sessionId"), qint64{session->value}}});
    settle();
    if (screen->sheet->sessionId() != session->value) {
        fail("navigationArgs.sessionId did not open the session");
    }
    exerciseTrainingKeyboard(*screen, *session, screenshotDirectory);
    exerciseTrainingMinimumWindow(engine, *screen, screenshotDirectory);
    exerciseTrainingSessionField(*screen, *session);
    exerciseTrainingReadOnly(engine);
    screen = openTrainingScreen(engine);
    if (!screen) {
        return;
    }
    exerciseTrainingLeaving(engine, *screen, *session);
}

// A: closing the window asks only with changes, and without them it closes at once. Each way has a
// window of its own, because a closed one is gone.
enum class CloseCase { Clean, StayThenDiscard, Save, FailedSaveThenDiscard };

void exerciseWindowClosing(CloseCase closeCase) {
    if (installRegistry(AppState::State::Ready) == nullptr) {
        fail("cannot install the service registry");
        return;
    }
    {
        const auto session = seedSheet(3);
        QQmlApplicationEngine engine;
        engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
        if (!session || engine.rootObjects().isEmpty()) {
            fail("the closing test has no window");
        } else if (auto screen = openTrainingScreen(engine)) {
            QQuickWindow* window = screen->window;
            const auto closesAtLast = [&](const char* reason) {
                waitUntil([&] { return !window->isVisible(); }, reason);
            };
            const auto closeAndExpectQuestion = [&](const char* reason) {
                window->close();
                settle();
                expectFlag(screen->question, "visible", true, reason);
                expectFlag(window, "visible", true, "the window closed before the answer");
            };
            if (closeCase == CloseCase::Clean) {
                window->close();
                closesAtLast("a window without changes did not close");
                expectFlag(screen->question, "visible", false,
                           "closing a window without changes asked");
            } else if (closeCase == CloseCase::StayThenDiscard) {
                screen->sheet->enterRpeDigit(0, 5);
                settle();
                closeAndExpectQuestion("closing with changes did not ask");
                pressKey(window, Qt::Key_Escape);
                expectFlag(window, "visible", true, "Stay closed the window");
                expectFlag(screen->sheet, "dirty", true, "Stay lost the changes");
                closeAndExpectQuestion("closing again with changes did not ask");
                QMetaObject::invokeMethod(screen->question, "dangerClicked");
                closesAtLast("Discard did not close the window");
            } else if (closeCase == CloseCase::Save) {
                screen->sheet->enterRpeDigit(0, 5);
                settle();
                closeAndExpectQuestion("closing with changes did not ask");
                QMetaObject::invokeMethod(screen->question, "primaryClicked");
                closesAtLast("Save did not close the window");
                const auto stored = ServiceRegistry::instance().training().sheet(*session);
                if (!stored || stored->rows[0].draft.rpe != 5) {
                    fail("Save on closing did not save");
                }
            } else {
                screen->sheet->setMinutesText(0, QStringLiteral("300"));
                settle();
                closeAndExpectQuestion("closing with changes did not ask");
                QMetaObject::invokeMethod(screen->question, "primaryClicked");
                settle();
                expectFlag(window, "visible", true, "a failed save closed the window");
                closeAndExpectQuestion("closing after a failed save did not ask");
                QMetaObject::invokeMethod(screen->question, "dangerClicked");
                closesAtLast("Discard after a failed save did not close the window");
            }
        }
    }
    settle();
    ServiceRegistry::reset();
}

// A tap as a pointer device sends it: a move to the point, then press and release, each with its
// own timestamp. clickItem() sends no timestamps, and a second one on the same point is not taken
// as a tap by the TapHandler of PlayerToken, which shares the item with a DragHandler.
void tapItem(QQuickWindow* window, QQuickItem* item) {
    static ulong timestamp = 1'000'000;
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = window->mapToGlobal(center);
    QMouseEvent move(QEvent::MouseMove, center, center, global, Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    move.setTimestamp(timestamp += 1000);
    QMouseEvent press(QEvent::MouseButtonPress, center, center, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    press.setTimestamp(timestamp += 1000);
    QMouseEvent release(QEvent::MouseButtonRelease, center, center, global, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    release.setTimestamp(timestamp += 50);
    QCoreApplication::sendEvent(window, &move);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    settle();
}

// A double tap: two taps close in time and place, as the TapHandler of PlayerToken counts them.
void doubleTapItem(QQuickWindow* window, QQuickItem* item) {
    static ulong timestamp = 500'000'000;
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = window->mapToGlobal(center);
    QMouseEvent move(QEvent::MouseMove, center, center, global, Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    move.setTimestamp(timestamp += 1000);
    QCoreApplication::sendEvent(window, &move);
    for (int tap = 0; tap < 2; ++tap) {
        QMouseEvent press(QEvent::MouseButtonPress, center, center, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        press.setTimestamp(timestamp += 100);
        QMouseEvent release(QEvent::MouseButtonRelease, center, center, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        release.setTimestamp(timestamp += 50);
        QCoreApplication::sendEvent(window, &press);
        QCoreApplication::sendEvent(window, &release);
    }
    settle();
}

// --- Pre-match (P12) ---

struct PreMatchScreen {
    QQuickWindow* window = nullptr;
    NavigationModel* navigation = nullptr;
    QQuickItem* view = nullptr;
    PreMatchViewModel* preMatch = nullptr;

    [[nodiscard]] QQuickItem* find(const QString& name) const {
        return findByObjectName(window->contentItem(), name);
    }
    [[nodiscard]] QQuickItem* token(int slot) const {
        return findByObjectName(view, QStringLiteral("token_%1").arg(slot));
    }
    [[nodiscard]] QVariant board(int slot, const char* role) const {
        return preMatch->board()->value(slot, role);
    }
    // A row of the squad list as the list shows it (LineupPlayersSortModel).
    [[nodiscard]] QVariant player(int row, const char* role) const {
        return preMatch->sortedPlayers()->value(row, role);
    }
    [[nodiscard]] QObject* slotPicker() const {
        return view->property("slotPicker").value<QObject*>();
    }
    [[nodiscard]] bool pickerOpen() const { return slotPicker()->property("opened").toBool(); }
    // Gives a token the focus and selects its slot without a click, which would open its picker.
    void focusToken(int slot) const {
        QMetaObject::invokeMethod(find(QStringLiteral("tacticsPitch")), "focusSlot",
                                  Q_ARG(QVariant, slot));
        preMatch->selectSlot(slot);
        settle();
    }
    [[nodiscard]] std::vector<qint64> boardPlayers() const {
        std::vector<qint64> ids;
        for (int slot = 0; slot < preMatch->board()->rowCount(); ++slot) {
            ids.push_back(board(slot, "playerId").toLongLong());
        }
        return ids;
    }
    // The first row of the squad list for which `accept` holds; -1: none.
    template <class Accept> [[nodiscard]] int playerRow(Accept accept) const {
        for (int row = 0; row < preMatch->sortedPlayers()->rowCount(); ++row) {
            if (accept(row)) {
                return row;
            }
        }
        return -1;
    }
    [[nodiscard]] bool hasIssue(const QString& ruleKey, qint64 playerId) const {
        for (int row = 0; row < preMatch->issues()->rowCount(); ++row) {
            if (preMatch->issues()->value(row, "ruleKey").toString() == ruleKey &&
                preMatch->issues()->value(row, "playerId").toLongLong() == playerId) {
                return true;
            }
        }
        return false;
    }
};

// Goes to the section (again, so it reads anew), finds it anew (a login builds the views again)
// and waits for the load statuses.
// matchId: opens that match (0: the one the view has). A change left by a failed step is dropped
// first, so leaving does not stop at the question.
std::optional<PreMatchScreen> openPreMatchScreen(QQmlApplicationEngine& engine,
                                                 qint64 matchId = 0) {
    PreMatchScreen screen;
    screen.window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    screen.navigation = screen.window->property("navigation").value<NavigationModel*>();
    if (QQuickItem* view = findQmlItem(screen.window->contentItem(), "PreMatchView")) {
        if (auto* open = view->property("viewModel").value<PreMatchViewModel*>()) {
            open->discardChanges();
        }
    }
    screen.navigation->activate(QStringLiteral("dashboard"));
    settle();
    screen.navigation->activate(QStringLiteral("prematch"),
                                matchId != 0 ? QVariantMap{{QStringLiteral("matchId"), matchId}}
                                             : QVariantMap{});
    settle();
    screen.view = findQmlItem(screen.window->contentItem(), "PreMatchView");
    if (screen.view == nullptr) {
        fail("the pre-match view is not loaded");
        return std::nullopt;
    }
    screen.preMatch = screen.view->property("viewModel").value<PreMatchViewModel*>();
    if (screen.preMatch == nullptr) {
        fail("the pre-match view has no viewmodel");
        return std::nullopt;
    }
    if (!waitUntil([&screen] { return !screen.preMatch->loadPending(); },
                   "the load statuses of pre-match did not arrive")) {
        return std::nullopt;
    }
    settle();
    renderFrame(screen.window);
    return screen;
}

// Presses Tab until `focused` holds, at most `limit` times.
template <class Focused>
bool tabUntil(QQuickWindow* window, Focused focused, int limit, const char* reason) {
    for (int press = 0; press < limit && !focused(); ++press) {
        pressKey(window, Qt::Key_Tab);
    }
    if (!focused()) {
        fail(reason);
        return false;
    }
    return true;
}

// Moves the current row of a focused list with the arrow keys.
void arrowTo(QQuickWindow* window, QQuickItem* list, int row) {
    int current = list->property("currentIndex").toInt();
    while (current != row) {
        pressKey(window, current < row ? Qt::Key_Down : Qt::Key_Up);
        const int moved = list->property("currentIndex").toInt();
        if (moved == current) {
            fail("the arrow keys do not move in the squad list");
            return;
        }
        current = moved;
    }
}

// The next planned match of the smoke team, read anew for every step: a step that failed and
// deleted or moved a match leaves no stale id behind. 0 with a failure: none.
qint64 nextPreMatchMatch(const char* step) {
    const auto next = ServiceRegistry::instance().lineup().nextPlannedMatch(smokeTeam());
    if (!next || !*next) {
        fail("pre-match: no planned match");
        qWarning("qml_smoke: pre-match step without a match: %s", step);
        return 0;
    }
    return (*next)->value;
}

// The precondition of the steps that need a saved lineup, through the service and not through an
// earlier step: a 4-4-2 of available players, by their primary position where one fits. The
// players of the slots in order; empty with a failure.
std::vector<qint64> saveSmokeLineup(qint64 matchId) {
    auto& lineups = ServiceRegistry::instance().lineup();
    const auto data =
        lineups.matchLineup(fdss::core::MatchId{matchId}, fdss::core::kDefaultYellowCardThreshold);
    const fdss::core::Formation* formation = fdss::core::findFormation("4-4-2");
    if (!data || formation == nullptr) {
        fail("pre-match: cannot read the match for a saved lineup");
        return {};
    }
    fdss::core::Lineup lineup = fdss::core::emptyLineup(*formation);
    std::set<fdss::core::PlayerId> used;
    const auto available = [&](const fdss::services::LineupCandidate& candidate) {
        return candidate.facts.statusOnMatchDay == fdss::core::PlayerStatus::Available &&
               !used.contains(candidate.player.id);
    };
    for (const bool samePosition : {true, false}) {
        for (fdss::core::LineupSlot& slot : lineup.pitchSlots) {
            if (slot.player) {
                continue;
            }
            for (const auto& candidate : data->candidates) {
                if (available(candidate) &&
                    (!samePosition || candidate.player.primaryPosition == slot.position)) {
                    slot.player = candidate.player.id;
                    used.insert(candidate.player.id);
                    break;
                }
            }
        }
    }
    if (!lineups.save(fdss::core::MatchId{matchId}, lineup)) {
        fail("pre-match: cannot save a lineup through the service");
        return {};
    }
    std::vector<qint64> players;
    for (const fdss::core::LineupSlot& slot : lineup.pitchSlots) {
        players.push_back(slot.player ? slot.player->value : 0);
    }
    return players;
}

// Ctrl+S as the keyboard sends it. A shortcut fires only in the focus window, and on Xvfb without
// a window manager (CI) the window may never get the focus: then the shortcut object is checked
// to be enabled and triggered as Qt would, and the run says so.
void pressSave(const PreMatchScreen& screen) {
    screen.window->requestActivate();
    QElapsedTimer waited;
    waited.start();
    while (QGuiApplication::focusWindow() != screen.window && waited.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (QGuiApplication::focusWindow() == screen.window) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier, QStringLiteral("s"));
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_S, Qt::ControlModifier, QStringLiteral("s"));
        QCoreApplication::sendEvent(screen.window, &press);
        QCoreApplication::sendEvent(screen.window, &release);
        settle();
        return;
    }
    qInfo("qml_smoke: the window has no focus (no window manager): Ctrl+S through its shortcut");
    auto* shortcut = screen.view->findChild<QObject*>(QStringLiteral("saveShortcut"));
    if (shortcut == nullptr || !shortcut->property("enabled").toBool()) {
        fail("the save shortcut is not there or not enabled");
        return;
    }
    QMetaObject::invokeMethod(shortcut, "activated");
    settle();
}

// The steps below are independent: each opens the screen on the next planned match read anew, sets
// up what it needs through the services and leaves no unsaved change behind, so one failure does
// not fail the steps after it (P12 CI).

// A click on a slot opens its picker; the injured player picked there names an error, replacing
// him in the squad list removes it.
void preMatchPickerAndIssues(QQmlApplicationEngine& engine, const QString& screenshotDirectory) {
    const qint64 matchId = nextPreMatchMatch("picker");
    auto screen = openPreMatchScreen(engine, matchId);
    if (!screen) {
        return;
    }
    PreMatchViewModel* preMatch = screen->preMatch;
    QQuickWindow* window = screen->window;
    if (preMatch->matchId() != matchId) {
        fail("pre-match does not open the next planned match");
        return;
    }
    preMatch->setProperty("formation", QStringLiteral("4-4-2"));
    settle();
    if (preMatch->formation() != QLatin1StringView("4-4-2") || screen->token(10) == nullptr) {
        fail("the board does not show 4-4-2");
        return;
    }

    // A click on the slot of a centre midfielder, then the injured player picked with Enter.
    const int injuredRow = screen->playerRow([&](int row) {
        return screen->player(row, "statusKey").toString() == QLatin1StringView("injured");
    });
    if (injuredRow < 0) {
        fail("the demo club has no injured player on the match day");
        return;
    }
    const qint64 injured = screen->player(injuredRow, "playerId").toLongLong();
    const QString injuredName = screen->player(injuredRow, "shortName").toString();
    // A click on a token selects it and opens its picker after the double click interval; the
    // picker of a centre midfielder starts with the centre midfielders (P12 manual test).
    tapItem(window, screen->token(6));
    if (preMatch->selectedSlot() != 6) {
        fail("a click on a token does not select its slot");
    }
    if (!waitUntil([&] { return screen->pickerOpen(); },
                   "a click on a slot does not open its picker")) {
        return;
    }
    settle();
    grabOrSave(window, screenshotDirectory, QStringLiteral("prematch-picker.png"));
    if (preMatch->slotPicker()->value(0, "group").toString() != QLatin1StringView("primary") ||
        preMatch->slotPicker()->rowCount() != preMatch->playerCount()) {
        fail("the picker of a slot does not start with its position");
    }
    int injuredPick = -1;
    for (int row = 0; row < preMatch->slotPicker()->rowCount(); ++row) {
        if (preMatch->slotPicker()->value(row, "playerId").toLongLong() == injured) {
            injuredPick = row;
        }
    }
    auto* pickerList = screen->find(QStringLiteral("slotPickerList"));
    if (pickerList == nullptr || injuredPick < 0) {
        fail("the picker does not list the injured player");
        return;
    }
    pickerList->forceActiveFocus(Qt::TabFocusReason);
    pickerList->setProperty("currentIndex", injuredPick);
    pressKey(window, Qt::Key_Return);
    settle();
    if (screen->pickerOpen()) {
        fail("Enter in the picker does not close it");
    }
    auto* list = screen->find(QStringLiteral("playerListView"));
    if (screen->board(6, "playerId").toLongLong() != injured ||
        !screen->hasIssue(QStringLiteral("unavailable"), injured) ||
        screen->board(6, "severity").toString() != QLatin1StringView("error")) {
        fail("an injured player in the XI is not an error");
    }
    bool named = false;
    for (int row = 0; row < preMatch->issues()->rowCount(); ++row) {
        named = named || preMatch->issues()->value(row, "text").toString().contains(injuredName);
    }
    if (!named) {
        fail("the error does not name the injured player");
    }
    // Replaced by an available player with XI (Enter) in the list: the error goes.
    screen->focusToken(6);
    const int replacementRow = screen->playerRow([&](int row) {
        return screen->player(row, "statusKey").toString() == QLatin1StringView("available") &&
               screen->player(row, "placement").toString().isEmpty();
    });
    list->forceActiveFocus(Qt::TabFocusReason);
    list->setProperty("currentIndex", replacementRow);
    pressKey(window, Qt::Key_Return);
    if (screen->hasIssue(QStringLiteral("unavailable"), injured)) {
        fail("replacing the injured player leaves the error");
    }

    preMatch->discardChanges();
    settle();
}

// 11 players by the keyboard alone, Ctrl+S, and the lineup reads back the same.
void preMatchKeyboardAndSave(QQmlApplicationEngine& engine) {
    const qint64 matchId = nextPreMatchMatch("keyboard");
    auto screen = openPreMatchScreen(engine, matchId);
    if (!screen) {
        return;
    }
    PreMatchViewModel* preMatch = screen->preMatch;
    QQuickWindow* window = screen->window;
    auto* list = screen->find(QStringLiteral("playerListView"));
    // The keyboard alone: Tab to the first token, Enter, Tab to the squad, then arrows and Enter.
    clickItem(window, screen->find(QStringLiteral("clearButton")));
    if (preMatch->startingCount() != 0) {
        fail("Clear leaves players in the lineup");
    }
    screen->find(QStringLiteral("formationField"))->forceActiveFocus(Qt::TabFocusReason);
    if (!tabUntil(
            window, [&] { return screen->token(0)->hasActiveFocus(); }, 20,
            "Tab does not reach the first token")) {
        return;
    }
    pressKey(window, Qt::Key_Return);
    if (preMatch->selectedSlot() != 0 || !screen->pickerOpen()) {
        fail("Enter on a token does not select its slot and open its picker");
    }
    // Esc closes the picker; the slot stays selected and its token gets the focus back.
    pressKey(window, Qt::Key_Escape);
    settle();
    if (screen->pickerOpen() || preMatch->selectedSlot() != 0 ||
        !screen->token(0)->hasActiveFocus()) {
        fail("Esc in the picker does not go back to the slot");
    }
    if (!tabUntil(
            window, [&] { return list->hasActiveFocus(); }, 40,
            "Tab does not reach the squad list")) {
        return;
    }
    // The list sorts the XI to the top, so every round looks for the next available player.
    for (int placed = 0; placed < 11; ++placed) {
        const int row = screen->playerRow([&](int candidate) {
            return screen->player(candidate, "statusKey").toString() ==
                       QLatin1StringView("available") &&
                   screen->player(candidate, "placement").toString().isEmpty();
        });
        if (row < 0) {
            break;
        }
        arrowTo(window, list, row);
        pressKey(window, Qt::Key_Return);
        // The next player of the rest is current, not the one who went to the XI.
        if (placed < 10 && !screen->player(list->property("currentIndex").toInt(), "placement")
                                .toString()
                                .isEmpty()) {
            fail("after Enter the squad list stays on the player who went to the XI");
        }
    }
    if (preMatch->startingCount() != 11 || preMatch->selectedSlot() != -1 ||
        !list->hasActiveFocus()) {
        fail("the keyboard path does not place 11 players");
    }

    pressSave(*screen);
    if (preMatch->dirty() || !preMatch->hasSavedLineup()) {
        fail("Ctrl+S does not save the lineup");
    }
    const std::vector<qint64> saved = screen->boardPlayers();
    preMatch->discardChanges();
    screen = openPreMatchScreen(engine, matchId);
    if (!screen) {
        return;
    }
    if (screen->boardPlayers() != saved || screen->preMatch->dirty()) {
        fail("the saved lineup does not read back the same");
    }
}

// The Calendar asks before deleting a match with a saved lineup (P12 review). On a match of its own
// four weeks after the next one, so even a delete without a question touches no other step.
void preMatchCalendarDelete(QQmlApplicationEngine& engine) {
    auto& calendar = ServiceRegistry::instance().calendar();
    const qint64 nextId = nextPreMatchMatch("calendar delete");
    const auto next = calendar.match(fdss::core::MatchId{nextId});
    if (!next) {
        fail("pre-match: cannot read the next match");
        return;
    }
    fdss::core::MatchDraft draft = *next;
    draft.date = fdss::core::Date{std::chrono::sys_days{next->date} + std::chrono::days{28}};
    const auto own = calendar.addMatch(smokeTeam(), draft);
    if (!own || saveSmokeLineup(own->value).empty()) {
        fail("pre-match: cannot add a match with a lineup to delete");
        return;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    navigation->activate(QStringLiteral("calendar"));
    settle();
    QQuickItem* calendarView = findQmlItem(window->contentItem(), "CalendarView");
    QMetaObject::invokeMethod(calendarView, "requestMatchDelete",
                              Q_ARG(QVariant, static_cast<qint64>(own->value)),
                              Q_ARG(QVariant, QStringLiteral("Rywal")));
    settle();
    auto* question = calendarView->property("deleteMatchDialog").value<QObject*>();
    if (question == nullptr || !question->property("visible").toBool()) {
        fail("deleting a match with a saved lineup does not ask first");
    }
    if (question != nullptr) {
        QMetaObject::invokeMethod(question, "close");
        settle();
    }
    if (!calendar.match(*own)) {
        fail("the question deleted the match before an answer");
        return;
    }
    if (!calendar.deleteMatch(*own)) {
        fail("pre-match: cannot remove the match of the delete step");
    }
}

// To the bench by keyboard, a refused second goalkeeper, and the touch sizes of the board.
void preMatchBenchKeys(QQmlApplicationEngine& engine) {
    const qint64 matchId = nextPreMatchMatch("bench");
    if (saveSmokeLineup(matchId).empty()) {
        return;
    }
    auto screen = openPreMatchScreen(engine, matchId);
    if (!screen) {
        return;
    }
    PreMatchViewModel* preMatch = screen->preMatch;
    QQuickWindow* window = screen->window;
    auto* list = screen->find(QStringLiteral("playerListView"));
    // To the bench by keyboard: B on a token, Tab to the button, B in the squad (P12 review).
    {
        const int bench = preMatch->benchCount();
        screen->focusToken(1);
        pressKey(window, Qt::Key_B);
        if (preMatch->benchCount() != bench + 1 || screen->board(1, "playerId").toLongLong() != 0) {
            fail("B on a token does not move its player to the bench");
        }
        screen->focusToken(2);
        auto* toBench = screen->find(QStringLiteral("toBenchButton"));
        if (tabUntil(
                window, [&] { return toBench->hasActiveFocus(); }, 30,
                "Tab does not reach the button Move to the bench")) {
            pressKey(window, Qt::Key_Return);
            if (preMatch->benchCount() != bench + 2) {
                fail("Enter on Move to the bench does not move the player");
            }
        }
        const int freeRow = screen->playerRow(
            [&](int row) { return screen->player(row, "placement").toString().isEmpty(); });
        list->forceActiveFocus(Qt::TabFocusReason);
        list->setProperty("currentIndex", freeRow);
        pressKey(window, Qt::Key_B);
        if (preMatch->benchCount() != bench + 3) {
            fail("B in the squad list does not add the player to the bench");
        }

        // A second goalkeeper picked in the position field: refused, the field shows the slot
        // again.
        screen->focusToken(3);
        auto* positionField = screen->find(QStringLiteral("positionField"));
        int gkIndex = -1;
        QMetaObject::invokeMethod(positionField, "indexOfValue", Q_RETURN_ARG(int, gkIndex),
                                  Q_ARG(QVariant, QStringLiteral("gk")));
        positionField->setProperty("currentIndex", gkIndex);
        QMetaObject::invokeMethod(positionField, "activated", Q_ARG(int, gkIndex));
        settle();
        if (preMatch->selectedPosition() == QLatin1StringView("gk") ||
            positionField->property("currentValue").toString() != preMatch->selectedPosition()) {
            fail("the position field keeps a refused goalkeeper");
        }

        // The controls of the board are touch sized (Theme.hitTouch).
        const auto touchSized = [](const QQuickItem* item) {
            return item != nullptr && item->height() >= 44 && item->width() >= 44;
        };
        for (const char* name : {"positionField", "swapButton", "toBenchButton", "removeButton",
                                 "benchRemove_0", "benchButton_0", "xiButton_0", "playerRow_0"}) {
            if (!touchSized(screen->find(QString::fromLatin1(name)))) {
                fail("a control of the tactics board is smaller than Theme.hitTouch");
                qWarning("qml_smoke: too small: %s", name);
            }
        }
        const QQuickItem* benchRemove = screen->find(QStringLiteral("benchRemove_0"));
        if (benchRemove == nullptr || benchRemove->parentItem()->parentItem()->height() < 44) {
            fail("a bench row is lower than Theme.hitTouch");
        }
        preMatch->discardChanges();
        settle();
    }
}

// Leaving with a change asks; Discard leaves and drops the change.
void preMatchLeaveGuard(QQmlApplicationEngine& engine) {
    const qint64 matchId = nextPreMatchMatch("leave guard");
    if (saveSmokeLineup(matchId).empty()) {
        return;
    }
    auto screen = openPreMatchScreen(engine, matchId);
    if (!screen) {
        return;
    }
    // Leaving with a change asks; Discard leaves and drops the change.
    screen->preMatch->swapSlots(1, 2);
    settle();
    expectFlag(screen->navigation, "leaveGuard", true, "a change of the lineup does not guard");
    screen->navigation->activate(QStringLiteral("dashboard"));
    settle();
    auto* question = screen->view->property("leaveDialog").value<QObject*>();
    if (question == nullptr || !question->property("visible").toBool() ||
        screen->navigation->currentKey() != QLatin1StringView("prematch")) {
        fail("leaving pre-match with a change does not ask");
    } else {
        QMetaObject::invokeMethod(question, "dangerClicked");
        settle();
        if (screen->navigation->currentKey() != QLatin1StringView("dashboard") ||
            screen->preMatch->dirty()) {
            fail("Discard does not leave pre-match");
        }
    }

    screen->preMatch->discardChanges();
    settle();
}

// A double click on a token opens the card of its player, after the question about a change; Back
// on the card returns to the same match. The same from a row of the squad list and from Load (P12
// manual test).
void preMatchPlayerCard(QQmlApplicationEngine& engine) {
    const qint64 matchId = nextPreMatchMatch("player card");
    if (saveSmokeLineup(matchId).empty()) {
        return;
    }
    auto screen = openPreMatchScreen(engine, matchId);
    if (!screen) {
        return;
    }
    QQuickWindow* window = screen->window;
    {
        const auto backTo = [&](const char* key, const char* reason) {
            QQuickItem* squadView = findQmlItem(window->contentItem(), "SquadView");
            auto* card = squadView != nullptr
                             ? squadView->property("playerCard").value<QQuickItem*>()
                             : nullptr;
            QQuickItem* back =
                card != nullptr ? findByObjectName(card, QStringLiteral("backButton")) : nullptr;
            if (screen->navigation->currentKey() != QLatin1StringView("squad") ||
                !squadView->property("cardOpen").toBool() || back == nullptr) {
                fail(reason);
                return qint64{0};
            }
            const qint64 shown = card->property("playerId").toLongLong();
            clickItem(window, back);
            settle();
            if (screen->navigation->currentKey() != QLatin1StringView(key)) {
                fail("Back on the card does not return where the card was opened from");
            }
            return shown;
        };
        const qint64 goalkeeper = screen->board(0, "playerId").toLongLong();
        screen->preMatch->swapSlots(1, 2);
        settle();
        doubleTapItem(window, screen->token(0));
        auto* leave = screen->view->property("leaveDialog").value<QObject*>();
        if (leave == nullptr || !leave->property("visible").toBool()) {
            fail("a double click with a change does not ask before leaving");
        } else {
            QMetaObject::invokeMethod(leave, "dangerClicked");
            settle();
        }
        if (backTo("prematch", "a double click on a token does not open the card") != goalkeeper ||
            screen->preMatch->matchId() != matchId) {
            fail("the card of a token is not the card of its player, or Back changes the match");
        }
        // The first click of the double one never opens the picker.
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval() + 100) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        if (screen->pickerOpen() || screen->preMatch->pickerSlot() >= 0) {
            fail("a double click on a token opens its picker");
        }

        const qint64 firstRow = screen->player(0, "playerId").toLongLong();
        // The first row in view: the list may be scrolled from the keyboard path.
        auto* rows = findByObjectName(screen->view, QStringLiteral("playerListView"));
        rows->setProperty("currentIndex", 0);
        QMetaObject::invokeMethod(rows, "positionViewAtBeginning");
        renderFrame(window);
        doubleTapItem(window, findByObjectName(screen->view, QStringLiteral("playerRow_0")));
        if (backTo("prematch", "a double click on a squad row does not open the card") !=
            firstRow) {
            fail("the card of a squad row is not the card of its player");
        }

        screen->navigation->activate(QStringLiteral("load"),
                                     QVariantMap{{QStringLiteral("playerId"), goalkeeper}});
        settle();
        QQuickItem* loadView = findQmlItem(window->contentItem(), "LoadView");
        if (loadView != nullptr &&
            waitUntil([&] { return loadView->property("hasSelection").toBool(); },
                      "Load does not select the player it was opened with")) {
            settle();
            clickItem(window, findByObjectName(loadView, QStringLiteral("playerCardButton")));
            settle();
            backTo("load", "the player card button of Load does not open the card");
            if (!loadView->property("hasSelection").toBool()) {
                fail("Back to Load loses the player");
            }
        }
    }
}

// The minimum window in both themes: tokens of Theme.hitTouch that do not overlap, icons clipped by
// a scrolled list, three tokens in one side zone.
void preMatchMinimumWindow(QQmlApplicationEngine& engine, const QString& screenshotDirectory) {
    const qint64 matchId = nextPreMatchMatch("minimum window");
    if (saveSmokeLineup(matchId).empty()) {
        return;
    }
    auto screen = openPreMatchScreen(engine, matchId);
    if (!screen) {
        return;
    }
    QQuickWindow* window = screen->window;
    auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                            QStringLiteral("AppSettings"));
    const bool startDark = settings->darkTheme();
    const QSize originalSize = window->size();
    window->resize(1280, 720);
    for (const bool dark : {true, false}) {
        settings->setDarkTheme(dark);
        settle();
        renderFrame(window);
        std::vector<QPointF> centres;
        for (int slot = 0; slot < 11; ++slot) {
            QQuickItem* token = screen->token(slot);
            if (token == nullptr || token->height() < 44 || token->width() < 44) {
                fail("a token is smaller than Theme.hitTouch");
                break;
            }
            centres.push_back(token->mapToScene(QPointF(token->width() / 2, 22)));
        }
        for (std::size_t a = 0; a < centres.size(); ++a) {
            for (std::size_t b = a + 1; b < centres.size(); ++b) {
                if (QLineF(centres[a], centres[b]).length() < 44) {
                    fail("two tokens overlap at the minimum window");
                }
            }
        }
        auto* issues = screen->find(QStringLiteral("lineupIssuesPanel"));
        auto* pitch = screen->find(QStringLiteral("tacticsPitch"));
        if (issues == nullptr || pitch == nullptr || issues->width() < 250 ||
            pitch->height() < 400 ||
            issues->mapToScene(QPointF(issues->width(), 0)).x() > window->width()) {
            fail("the board does not fit the minimum window");
        }
        grabOrSave(window, screenshotDirectory,
                   QStringLiteral("prematch-%1.png")
                       .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
    }
    settings->setDarkTheme(startDark);

    // A scrolled list clips the icons of its rows: below the squad list, with the issues panel
    // hidden, there is only the page background (fix: icons respect clipping in lists).
    {
        auto* rows = screen->find(QStringLiteral("playerListView"));
        auto* panel = screen->find(QStringLiteral("lineupIssuesPanel"));
        const QColor background = window->color();
        panel->setOpacity(0);
        // Half a row up: the last row in view is cut, the next ones lie below the list.
        rows->setProperty("contentY", rows->property("originY").toReal() + 22);
        settle();
        const QImage image = window->grabWindow();
        const qreal ratio = image.devicePixelRatio();
        const QRectF below = rows->mapRectToScene(QRectF(0, rows->height() + 6, rows->width(), 56));
        int foreign = 0;
        for (int y = int(below.top() * ratio); y < int(below.bottom() * ratio); ++y) {
            for (int x = int(below.left() * ratio); x < int(below.right() * ratio); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (std::abs(pixel.red() - background.red()) > 8 ||
                    std::abs(pixel.green() - background.green()) > 8 ||
                    std::abs(pixel.blue() - background.blue()) > 8) {
                    ++foreign;
                }
            }
        }
        if (foreign > 0) {
            fail("icons of rows outside a scrolled list are drawn below it");
            qWarning("qml_smoke: %d pixels below the squad list", foreign);
            image.save(QDir::temp().filePath(QStringLiteral("fdss-clip.png")));
        }
        panel->setOpacity(1);
        rows->setProperty("contentY", rows->property("originY"));
        settle();
    }

    // Three tokens in one side zone at the minimum window: they do not overlap (P12 review).
    for (const int slot : {8, 9, 10}) {
        screen->preMatch->setSlotPosition(slot, QStringLiteral("lw"));
    }
    settle();
    renderFrame(window);
    std::vector<QRectF> wingers;
    for (const int slot : {8, 9, 10}) {
        if (screen->board(slot, "positionKey").toString() != QLatin1StringView("lw")) {
            fail("a slot did not move to the left wing zone");
        }
        QQuickItem* token = screen->token(slot);
        wingers.push_back(token->mapRectToScene(QRectF(0, 0, token->width(), token->height())));
    }
    for (std::size_t a = 0; a < wingers.size(); ++a) {
        for (std::size_t b = a + 1; b < wingers.size(); ++b) {
            if (wingers[a].intersects(wingers[b])) {
                fail("tokens of one zone overlap at the minimum window");
            }
        }
    }
    for (int a = 0; a < 11; ++a) {
        for (int b = a + 1; b < 11; ++b) {
            const QPointF first =
                screen->token(a)->mapToScene(QPointF(screen->token(a)->width() / 2, 22));
            const QPointF second =
                screen->token(b)->mapToScene(QPointF(screen->token(b)->width() / 2, 22));
            if (QLineF(first, second).length() < 44) {
                fail("two tokens overlap with three left wingers");
            }
        }
    }
    grabOrSave(window, screenshotDirectory, QStringLiteral("prematch-three-wingers.png"));
    screen->preMatch->discardChanges();
    window->resize(originalSize);
    settle();
}

// Another role: the saved lineup, read-only.
void preMatchReadOnly(QQmlApplicationEngine& engine) {
    const qint64 matchId = nextPreMatchMatch("read-only");
    const std::vector<qint64> saved = saveSmokeLineup(matchId);
    if (saved.empty()) {
        return;
    }
    const auto physio = addSmokeStaff("Fizjo Smoke", fdss::core::Role::Physio);
    if (!physio || !logInAgain(engine, *physio)) {
        return;
    }
    auto screen = openPreMatchScreen(engine, matchId);
    if (!screen) {
        return;
    }
    if (!screen->preMatch->readOnly() || screen->boardPlayers() != saved) {
        fail("another role does not see the saved lineup read-only");
    }
    expectFlag(screen->find(QStringLiteral("tacticsPitch")), "interactive", false,
               "the board is interactive for a role without EditLineup");
    expectFlag(screen->find(QStringLiteral("saveButton")), "visible", false,
               "a role without EditLineup sees Save");
}

// Demo data -> next match -> 4-4-2 -> an injured player names an error, replacing him removes it;
// 11 players by the keyboard alone; Ctrl+S; the lineup reads back the same; leaving asks; both
// themes at the minimum window; another role sees the board read-only (packet P12, Done when).
void exercisePreMatch(QQmlApplicationEngine& engine, const QString& screenshotDirectory) {
    if (!ServiceRegistry::instance().demoData().load(smokeTeam())) {
        fail("cannot load the demo club for pre-match");
        return;
    }
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        fail("Main.qml did not load");
        return;
    }
    settle();
    preMatchPickerAndIssues(engine, screenshotDirectory);
    preMatchKeyboardAndSave(engine);
    preMatchCalendarDelete(engine);
    preMatchBenchKeys(engine);
    preMatchLeaveGuard(engine);
    preMatchPlayerCard(engine);
    preMatchMinimumWindow(engine, screenshotDirectory);
    // Last: it logs in another user.
    preMatchReadOnly(engine);
}

// --- Match Day (P13) ---

struct MatchDayScreen {
    QQuickWindow* window = nullptr;
    NavigationModel* navigation = nullptr;
    QQuickItem* view = nullptr;
    MatchDayViewModel* matchDay = nullptr;

    [[nodiscard]] QQuickItem* find(const QString& name) const {
        return findByObjectName(view, name);
    }
    // The tile of a player on the pitch ("pitchTile") or on the bench ("benchTile").
    [[nodiscard]] QQuickItem* tile(const QString& kind, qint64 playerId) const {
        QQuickItem* found = nullptr;
        const std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
            if (found != nullptr) {
                return;
            }
            if (item->objectName() == kind && item->property("playerId").toLongLong() == playerId) {
                found = item;
                return;
            }
            for (QQuickItem* child : item->childItems()) {
                walk(child);
            }
        };
        walk(view);
        return found;
    }
    // Each renders a frame first: a pending action shows a bar that moves the tiles down, and the
    // position must be the one on screen.
    void click(const QString& name) const {
        renderFrame(window);
        QQuickItem* item = find(name);
        if (item == nullptr || !item->isVisible() || !item->isEnabled()) {
            qWarning("qml_smoke: match day: %s is missing, hidden or disabled",
                     qUtf8Printable(name));
            fail("match day: a button to click is not there");
            return;
        }
        clickItem(window, item);
        renderFrame(window);
    }
    void tap(const QString& kind, qint64 playerId) const {
        renderFrame(window);
        QQuickItem* item = tile(kind, playerId);
        if (item == nullptr || !item->isEnabled()) {
            qWarning("qml_smoke: match day: no enabled %s for player %lld", qUtf8Printable(kind),
                     playerId);
            fail("match day: a player tile to tap is not there");
            return;
        }
        const QPointF c = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        // The tiles scroll at a small window.
        QMetaObject::invokeMethod(find(QStringLiteral("tilesArea")), "reveal",
                                  Q_ARG(QVariant, QVariant::fromValue(item)));
        renderFrame(window);
        clickItem(window, item);
        renderFrame(window);
    }
};

// Resizes the window and waits until its content has the new size. On xcb (Xvfb on CI) a resize
// lands asynchronously: a measure right after resize() reads the old layout (P13b CI failure).
bool resizeAndWait(QQuickWindow* window, QSize size) {
    window->resize(size);
    const bool done = waitUntil(
        [&] {
            return window->contentItem()->width() == size.width() &&
                   window->contentItem()->height() == size.height();
        },
        "the window does not take its new size");
    settle();
    return done;
}

// qml_smoke --without-focus: Match Day runs while another window holds the focus, as on Xvfb
// without a window manager (CI), where the window may never get it.
bool g_withoutFocus = false;

void takeFocusAway(QQuickWindow* window) {
    static QWindow* thief = nullptr;
    if (thief == nullptr) {
        thief = new QWindow();
        thief->resize(10, 10);
        thief->setPosition(window->x() + window->width() + 20, window->y());
        thief->show();
        qInfo("qml_smoke: match day runs without the focus of its window");
    }
    thief->requestActivate();
    QElapsedTimer waited;
    waited.start();
    while (QGuiApplication::focusWindow() == window && waited.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

// Goes to Match Day with the match (0: the default one) and finds the view anew (a login builds
// the views again). The view starts clean: the tiles, no pending action (the view stays loaded
// between the steps).
std::optional<MatchDayScreen> openMatchDayScreen(QQmlApplicationEngine& engine,
                                                 qint64 matchId = 0) {
    MatchDayScreen screen;
    screen.window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    screen.navigation = screen.window->property("navigation").value<NavigationModel*>();
    // The steps prepare their matches through the services, behind a view that stays loaded: it
    // reads its match again first, so a match an earlier step left live and the service has
    // finished does not ask "Leave the live match?" on the way to the next one.
    if (QQuickItem* loaded = findQmlItem(screen.window->contentItem(), "MatchDayView")) {
        // A dialog an earlier step left open would cover the screen of this one.
        for (const char* dialog : {"startMatchDialog", "leaveDialog", "noteEditor",
                                   "tooFewPlayersDialog", "setClockEditor"}) {
            auto* popup = loaded->property(dialog).value<QObject*>();
            if (popup != nullptr && popup->property("visible").toBool()) {
                QMetaObject::invokeMethod(popup, "close");
            }
        }
        if (auto* viewModel = loaded->property("viewModel").value<MatchDayViewModel*>()) {
            viewModel->reload();
            settle();
        }
    }
    screen.navigation->activate(QStringLiteral("matchday"),
                                matchId != 0 ? QVariantMap{{QStringLiteral("matchId"), matchId}}
                                             : QVariantMap{});
    settle();
    screen.view = findQmlItem(screen.window->contentItem(), "MatchDayView");
    if (screen.view == nullptr) {
        fail("the match day view is not loaded");
        return std::nullopt;
    }
    screen.matchDay = screen.view->property("viewModel").value<MatchDayViewModel*>();
    if (screen.matchDay == nullptr) {
        fail("the match day view has no viewmodel");
        return std::nullopt;
    }
    screen.view->setProperty("boardMode", QStringLiteral("players"));
    screen.matchDay->cancelAction();
    if (g_withoutFocus) {
        takeFocusAway(screen.window);
    }
    settle();
    renderFrame(screen.window);
    return screen;
}

// A 4-4-2 of available players (saveSmokeLineup) and three more available players on the bench.
// The XI in slot order, then the bench; empty with a failure.
std::vector<qint64> saveMatchDayLineup(qint64 matchId) {
    std::vector<qint64> players = saveSmokeLineup(matchId);
    if (players.empty()) {
        return {};
    }
    auto& lineups = ServiceRegistry::instance().lineup();
    auto data =
        lineups.matchLineup(fdss::core::MatchId{matchId}, fdss::core::kDefaultYellowCardThreshold);
    if (!data) {
        fail("match day: cannot read the saved lineup");
        return {};
    }
    for (const auto& candidate : data->candidates) {
        if (data->lineup.bench.size() == 3) {
            break;
        }
        if (candidate.facts.statusOnMatchDay == fdss::core::PlayerStatus::Available &&
            std::ranges::find(players, candidate.player.id.value) == players.end()) {
            data->lineup.bench.push_back(candidate.player.id);
        }
    }
    if (data->lineup.bench.size() != 3 ||
        !lineups.save(fdss::core::MatchId{matchId}, data->lineup)) {
        fail("match day: cannot save a bench");
        return {};
    }
    for (const fdss::core::PlayerId player : data->lineup.bench) {
        players.push_back(player.value);
    }
    return players;
}

// Every visible tile and button of the screen is at least Theme.hitTouch (44 px) both ways.
void expectMatchDayHitTargets(const MatchDayScreen& screen, const char* when) {
    constexpr qreal kHitTouch = 44;
    int checked = 0;
    const std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (!item->isVisible()) {
            return;
        }
        const QString name = item->objectName();
        const bool target = name == QLatin1StringView("pitchTile") ||
                            name == QLatin1StringView("benchTile") ||
                            name == QLatin1StringView("liveBenchTile") ||
                            name == QLatin1StringView("liveFormationField") ||
                            name == QLatin1StringView("livePositionField") ||
                            name.startsWith(QLatin1StringView("token_")) ||
                            name.endsWith(QLatin1StringView("Button"));
        if (target) {
            ++checked;
            if (item->width() < kHitTouch || item->height() < kHitTouch) {
                qWarning("qml_smoke: match day (%s): %s is %.0f x %.0f", when, qUtf8Printable(name),
                         item->width(), item->height());
                fail("a match day hit target is smaller than 44 px");
            }
        }
        for (QQuickItem* child : item->childItems()) {
            walk(child);
        }
    };
    walk(screen.view);
    if (checked < 20) {
        fail("match day: too few hit targets to measure");
    }
}

// The buttons of an open dialog (header and footer) are at least 44 px both ways.
void expectPopupHitTargets(QObject* popup, const char* when) {
    constexpr qreal kHitTouch = 44;
    int checked = 0;
    const std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (!item->isVisible()) {
            return;
        }
        if (item->objectName().endsWith(QLatin1StringView("Button"))) {
            ++checked;
            if (item->width() < kHitTouch || item->height() < kHitTouch) {
                qWarning("qml_smoke: match day (%s): %s is %.0f x %.0f", when,
                         qUtf8Printable(item->objectName()), item->width(), item->height());
                fail("a button of a match day dialog is smaller than 44 px");
            }
        }
        for (QQuickItem* child : item->childItems()) {
            walk(child);
        }
    };
    for (const char* part : {"header", "footer"}) {
        if (auto* item = popup->property(part).value<QQuickItem*>()) {
            walk(item);
        }
    }
    if (checked < 2) {
        fail("match day: the buttons of a dialog were not found to measure");
    }
}

// The accelerators of Match Day: enabled or not as expected; with the window focused, the key is
// also sent and must change nothing when they are off (`unchanged` holds afterwards).
template <class Unchanged>
void expectShortcutsOff(const MatchDayScreen& screen, bool off, Unchanged unchanged,
                        const char* reason) {
    for (const char* name : {"goalShortcut", "opponentGoalShortcut", "yellowShortcut",
                             "redShortcut", "subShortcut", "noteShortcut", "undoShortcut"}) {
        auto* shortcut = screen.view->findChild<QObject*>(QString::fromLatin1(name));
        if (shortcut == nullptr) {
            fail("match day: a shortcut is not there");
            return;
        }
        if (off && shortcut->property("enabled").toBool()) {
            qWarning("qml_smoke: match day: %s is enabled", name);
            fail(reason);
        }
    }
    if (!off) {
        return;
    }
    screen.window->requestActivate();
    QElapsedTimer waited;
    waited.start();
    while (QGuiApplication::focusWindow() != screen.window && waited.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (QGuiApplication::focusWindow() != screen.window) {
        qInfo("qml_smoke: the window has no focus (no window manager): match day keys not sent");
        return;
    }
    for (const auto& [key, modifiers, text] :
         {std::tuple{Qt::Key_G, Qt::NoModifier, QStringLiteral("g")},
          std::tuple{Qt::Key_S, Qt::NoModifier, QStringLiteral("s")},
          std::tuple{Qt::Key_Z, Qt::ControlModifier, QStringLiteral("z")}}) {
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
        QCoreApplication::sendEvent(screen.window, &press);
        QCoreApplication::sendEvent(screen.window, &release);
        settle();
    }
    if (!unchanged()) {
        fail(reason);
    }
}

// The start dialog with a custom format, in one language: the fields of the parts and of their
// length and their labels lie inside the dialog and the window, no label is cut; the sum error
// shows only once a value is finished (P13 manual test).
void expectStartDialogLayout(QQmlApplicationEngine& engine, QQuickWindow* window, QObject* dialog,
                             const QString& language) {
    auto* languages = engine.singletonInstance<LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));
    languages->setLanguage(language);
    dialog->setProperty("chosenFormat", QStringLiteral("custom"));
    settle();
    renderFrame(window);
    const QRectF box(dialog->property("x").toReal(), dialog->property("y").toReal(),
                     dialog->property("width").toReal(), dialog->property("height").toReal());
    const QRectF screen(0, 0, window->width(), window->height());
    for (const char* name :
         {"periodsFormInput", "periodLengthFormInput", "periodsInput", "periodLengthInput"}) {
        auto* item = dialog->property(name).value<QQuickItem*>();
        if (item == nullptr || !item->isVisible()) {
            fail("match day: a field of the start dialog is not there");
            continue;
        }
        const QRectF rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        if (!box.contains(rect) || !screen.contains(rect)) {
            qWarning("qml_smoke: match day (%s): %s at %.0f,%.0f %.0fx%.0f outside the dialog "
                     "%.0f,%.0f %.0fx%.0f",
                     qUtf8Printable(language), name, rect.x(), rect.y(), rect.width(),
                     rect.height(), box.x(), box.y(), box.width(), box.height());
            fail("match day: a field of the start dialog does not fit");
        }
        const std::function<void(QQuickItem*)> labels = [&](QQuickItem* child) {
            if (child->property("truncated").toBool()) {
                qWarning("qml_smoke: match day (%s): cut text '%s'", qUtf8Printable(language),
                         qUtf8Printable(child->property("text").toString()));
                fail("match day: a label of the start dialog is cut");
            }
            for (QQuickItem* grandchild : child->childItems()) {
                labels(grandchild);
            }
        };
        labels(item);
    }
    // 4 × 40 typed: no error while typing, the sum error once the field is left.
    auto* periods = dialog->property("periodsInput").value<QQuickItem*>();
    auto* length = dialog->property("periodLengthInput").value<QQuickItem*>();
    auto* lengthForm = dialog->property("periodLengthFormInput").value<QQuickItem*>();
    // Which field has the focus decides; items have the active focus only in the focus window.
    activateWindow(window);
    periods->forceActiveFocus();
    periods->setProperty("value", 4);
    length->forceActiveFocus(); // from one field to the other: still typing
    QMetaObject::invokeMethod(length, "increase");
    settle();
    if (!lengthForm->property("error").toString().isEmpty()) {
        fail("match day: the sum error shows while the value is typed");
    }
    // Both fields left.
    dialog->property("formatInput").value<QQuickItem*>()->forceActiveFocus();
    settle();
    if (lengthForm->property("error").toString().isEmpty()) {
        fail("match day: the sum error does not show once the value is finished");
    }
    dialog->setProperty("chosenFormat", QStringLiteral("2x45"));
    // The focus back on the dialog, as when it opens: Enter starts.
    dialog->property("formatInput").value<QQuickItem*>()->setFocus(false);
    dialog->property("contentItem").value<QQuickItem*>()->forceActiveFocus();
    settle();
}

int smokeMinutes(fdss::data::DatabaseManager& database, qint64 matchId, qint64 playerId,
                 const char* column) {
    QSqlQuery query(database.database());
    query.prepare(QStringLiteral("SELECT %1 FROM appearance WHERE match_id = ? AND player_id = ?")
                      .arg(QString::fromLatin1(column)));
    query.addBindValue(matchId);
    query.addBindValue(playerId);
    if (!query.exec() || !query.next() || query.value(0).isNull()) {
        return -1;
    }
    return query.value(0).toInt();
}

// Demo data -> planned match -> lineup -> Match Day by clicks and taps alone: two goals, a
// substitution, a yellow and a second yellow, undo, the end; the minutes in the database match a
// hand count (packet P13, Done when). Leaving the live match asks; a login opens the live match,
// read-only for another role.
// Live tactics (P13b), at 30:20 of the first half: the Tactics view in both themes at the minimum
// window (tokens of Theme.hitTouch that do not overlap, the board and the timeline inside the
// window), 4-4-2 → 4-2-3-1 confirmed at 30', on the timeline, then undone. Back to the tiles.
// Texts are compared with their translation in the language shown: the language the earlier
// scenarios leave depends on the system one (Polish on a Polish desktop, English on CI).
void exerciseLiveTactics(QQmlApplicationEngine& engine, const MatchDayScreen& screen,
                         const QString& screenshotDirectory) {
    MatchDayViewModel* matchDay = screen.matchDay;
    QQuickItem* modeSwitch = screen.find(QStringLiteral("boardModeSwitch"));
    if (modeSwitch == nullptr) {
        fail("match day: the Players / Tactics switch is not there");
        return;
    }
    QMetaObject::invokeMethod(modeSwitch, "select", Q_ARG(QVariant, QVariant(1)));
    settle();
    renderFrame(screen.window);
    auto* tactics = screen.find(QStringLiteral("tacticsArea"));
    auto* tiles = screen.find(QStringLiteral("tilesArea"));
    if (tactics == nullptr || !tactics->isVisible() || tiles == nullptr || tiles->isVisible()) {
        fail("match day: Tactics does not show the board in place of the tiles");
        return;
    }
    if (matchDay->shapeText() != QLatin1StringView("4-4-2")) {
        fail("match day: the board does not show the 4-4-2 of the lineup");
    }
    expectMatchDayHitTargets(screen, "tactics");

    auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                            QStringLiteral("AppSettings"));
    const bool startDark = settings->darkTheme();
    const QSize originalSize = screen.window->size();
    resizeAndWait(screen.window, QSize(1280, 720));
    for (const bool dark : {true, false}) {
        settings->setDarkTheme(dark);
        settle();
        renderFrame(screen.window);
        std::vector<QPointF> centres;
        for (int slot = 0; slot < 11; ++slot) {
            QQuickItem* token = screen.find(QStringLiteral("token_%1").arg(slot));
            if (token == nullptr || !token->isVisible() || token->height() < 44 ||
                token->width() < 44) {
                fail("match day: a token of the live board is missing or smaller than 44 px");
                break;
            }
            centres.push_back(token->mapToScene(QPointF(token->width() / 2, 22)));
        }
        for (std::size_t a = 0; a < centres.size(); ++a) {
            for (std::size_t b = a + 1; b < centres.size(); ++b) {
                if (QLineF(centres[a], centres[b]).length() < 44) {
                    fail("match day: two tokens of the live board overlap at the minimum window");
                }
            }
        }
        // The circle and the name of every token are apart from those of every other token
        // (P13b review), for 4-4-2 and for 3-5-2 (three centre backs in one zone).
        const auto expectTextApart = [&](const char* shape) {
            renderFrame(screen.window);
            std::vector<std::vector<QRectF>> parts;
            for (int slot = 0; slot < 11; ++slot) {
                QQuickItem* token = screen.find(QStringLiteral("token_%1").arg(slot));
                std::vector<QRectF> rects;
                for (const char* name : {"tokenCircle", "tokenLabel"}) {
                    auto* part = token != nullptr
                                     ? findByObjectName(token, QString::fromLatin1(name))
                                     : nullptr;
                    if (part != nullptr && part->isVisible()) {
                        rects.push_back(
                            part->mapRectToScene(QRectF(0, 0, part->width(), part->height())));
                    }
                }
                parts.push_back(std::move(rects));
            }
            for (std::size_t a = 0; a < parts.size(); ++a) {
                for (std::size_t b = a + 1; b < parts.size(); ++b) {
                    for (const QRectF& first : parts[a]) {
                        for (const QRectF& second : parts[b]) {
                            if (first.intersects(second)) {
                                qWarning("qml_smoke: match day (%s): tokens %zu and %zu overlap",
                                         shape, a, b);
                                fail("match day: the texts of two tokens overlap at the minimum "
                                     "window");
                            }
                        }
                    }
                }
            }
        };
        expectTextApart("4-4-2");
        if (!matchDay->changeFormation(QStringLiteral("3-5-2"))) {
            fail("match day: 3-5-2 cannot be chosen on the live board");
        }
        settle();
        expectTextApart("3-5-2");
        matchDay->cancelAction();
        settle();
        auto* pitch = screen.find(QStringLiteral("livePitch"));
        auto* bench = screen.find(QStringLiteral("liveBench"));
        auto* timeline = screen.find(QStringLiteral("timeline"));
        if (pitch == nullptr || bench == nullptr || timeline == nullptr || pitch->height() < 250 ||
            bench->mapToScene(QPointF(bench->width(), 0)).x() >
                timeline->mapToScene(QPointF(0, 0)).x() ||
            timeline->mapToScene(QPointF(timeline->width(), 0)).x() > screen.window->width()) {
            fail("match day: the live board does not fit the minimum window");
        }
        expectMatchDayHitTargets(screen,
                                 dark ? "tactics, dark, 1280 x 720" : "tactics, 1280 x 720");
        grabOrSave(screen.window, screenshotDirectory,
                   QStringLiteral("match-day-tactics-%1.png")
                       .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")));
    }
    settings->setDarkTheme(startDark);
    resizeAndWait(screen.window, originalSize);
    renderFrame(screen.window);

    // 4-2-3-1 from the formation field: a draft until Confirm, at 30' typed in the minute field.
    auto* formation = screen.find(QStringLiteral("liveFormationField"));
    if (formation == nullptr || !formation->isEnabled()) {
        fail("match day: the formation field of the live board is missing or disabled");
        return;
    }
    auto* livePitch = screen.find(QStringLiteral("livePitch"));
    const qreal pitchHeight = livePitch != nullptr ? livePitch->height() : 0;
    QMetaObject::invokeMethod(formation, "valueActivated",
                              Q_ARG(QVariant, QVariant(QStringLiteral("4-2-3-1"))));
    settle();
    renderFrame(screen.window);
    if (livePitch == nullptr || livePitch->height() != pitchHeight) {
        fail("match day: the board changes its size while a change of shape waits");
    }
    // Next to the board, which keeps its size: the bar above the tiles stays hidden.
    expectFlag(screen.find(QStringLiteral("pendingBar")), "visible", false,
               "match day: the pending bar takes height from the board");
    auto* confirm = screen.find(QStringLiteral("liveSaveActionButton"));
    if (matchDay->pendingAction() != QLatin1StringView("tactic") || confirm == nullptr ||
        !confirm->isVisible() ||
        confirm->property("text").toString() !=
            QCoreApplication::translate("MatchDayView", "Confirm")) {
        fail("match day: a change of shape does not wait for Confirm");
        return;
    }
    if (auto* minute = screen.find(QStringLiteral("liveMinuteField"))) {
        minute->forceActiveFocus();
        QMetaObject::invokeMethod(minute, "selectAll");
        typeText(screen.window, QStringLiteral("30"));
    } else {
        fail("match day: the minute field of the change of shape is not there");
    }
    screen.click(QStringLiteral("liveSaveActionButton"));
    settle();
    // The latest in the match first: the opponent goal at 31', then the change at 30'.
    const auto tacticRow = [&]() -> int {
        for (int row = 0; row < matchDay->timeline()->rowCount(); ++row) {
            if (matchDay->timeline()->value(row, "kind") == QStringLiteral("tactic")) {
                return row;
            }
        }
        return -1;
    };
    const int row = tacticRow();
    if (matchDay->pendingAction() != QLatin1StringView("") || row < 0 ||
        matchDay->timeline()->value(row, "minuteText") != QStringLiteral("30'") ||
        matchDay->timeline()->value(row, "playerText") != QStringLiteral("4-4-2 → 4-2-3-1") ||
        matchDay->shapeText() != QLatin1StringView("4-2-3-1")) {
        fail("match day: the change of shape at 30' is not on the timeline");
    }
    screen.click(QStringLiteral("undoButton"));
    settle();
    if (tacticRow() >= 0 || matchDay->shapeText() != QLatin1StringView("4-4-2")) {
        fail("match day: undo does not take back the change of shape");
    }
    // A swap started on the board ends with the view (P13b review): leave Match Day with a swap
    // waiting, come back, and a plain click on a token only selects it.
    {
        // The strikers, at the top of the pitch: no toast covers them.
        QQuickItem* token = screen.find(QStringLiteral("token_9"));
        if (token == nullptr) {
            fail("match day: no token to start a swap from");
            return;
        }
        tapItem(screen.window, token);
        renderFrame(screen.window);
        if (matchDay->selectedSlot() != 9) {
            fail("match day: a click on a token does not select it");
        }
        screen.click(QStringLiteral("liveSwapButton"));
        if (screen.view->property("swapFrom").toInt() != 9) {
            fail("match day: the swap button does not start a swap");
        }
        screen.navigation->activate(QStringLiteral("dashboard"));
        settle();
        auto* question = screen.view->property("leaveDialog").value<QObject*>();
        if (question == nullptr || !question->property("visible").toBool()) {
            fail("match day: leaving the live match does not ask");
            return;
        }
        QMetaObject::invokeMethod(question, "primaryClicked");
        settle();
        if (screen.navigation->currentKey() != QLatin1StringView("dashboard")) {
            fail("match day: Leave does not leave the live match");
        }
        screen.navigation->activate(QStringLiteral("matchday"));
        settle();
        renderFrame(screen.window);
        if (screen.view->property("swapFrom").toInt() != -1 ||
            screen.view->property("boardMode").toString() != QLatin1StringView("tactics")) {
            fail("match day: a swap started before leaving is still waiting after the return");
        }
        token = screen.find(QStringLiteral("token_10"));
        if (token == nullptr) {
            fail("match day: no token to click after the return");
            return;
        }
        tapItem(screen.window, token);
        renderFrame(screen.window);
        if (!matchDay->pendingAction().isEmpty() || matchDay->tacticsDirty() ||
            matchDay->selectedSlot() != 10) {
            fail("match day: a plain click after the return makes a swap");
        }
        // A switch of the view ends a swap too.
        screen.click(QStringLiteral("liveSwapButton"));
        QMetaObject::invokeMethod(modeSwitch, "select", Q_ARG(QVariant, QVariant(0)));
        settle();
        if (screen.view->property("swapFrom").toInt() != -1) {
            fail("match day: Players does not end a swap waiting on the board");
        }
    }
    renderFrame(screen.window);
    if (tiles == nullptr || !tiles->isVisible()) {
        fail("match day: Players does not bring the tiles back");
    }
}

// The Match Day steps (P13, P13b review) are independent, as those of Pre-match: each one gets its
// own match of today through the services (the demo has a single planned one), with the lineup, the
// format and the history it needs, and opens the screen in a clean state. A failure stays in its
// step instead of leaving the screen in a state that fails every step after it.

struct MatchDaySetup {
    qint64 matchId = 0;
    // The XI in slot order (a 4-4-2), then the bench of three.
    std::vector<qint64> players;
};

// A team has at most one live match: what an earlier step left live is finished (or taken back to
// planned before its kick-off) through the service.
void finishLiveMatch() {
    auto& service = ServiceRegistry::instance().matchDay();
    for (int step = 0; step < 8; ++step) {
        const auto live = service.liveMatch(smokeTeam());
        if (!live) {
            fail("match day: cannot read the live match");
            return;
        }
        if (!*live) {
            return;
        }
        const auto state = service.state(**live);
        if (!state || !state->clock) {
            fail("match day: cannot read the live match of an earlier step");
            return;
        }
        const auto phase = fdss::core::phaseOf(*state->clock);
        const bool done = phase == fdss::core::MatchPhase::NotStarted
                              ? service.cancelStart(**live).has_value()
                          : state->clock->clockless || fdss::core::onPitchCount(state->situation) <
                                                           fdss::core::kMinPlayersOnPitch
                              ? service.endMatch(**live).has_value()
                              : service.advanceClock(**live).has_value();
        if (!done) {
            fail("match day: cannot finish the live match of an earlier step");
            return;
        }
    }
    fail("match day: the live match of an earlier step does not finish");
}

// A new match of today against the first opponent with a 4-4-2 and a bench of three
// (saveMatchDayLineup); `kickOff`: started as 2 × 45 with the clock and kicked off now.
std::optional<MatchDaySetup> newMatchDayMatch(const char* step, bool kickOff) {
    finishLiveMatch();
    auto& calendar = ServiceRegistry::instance().calendar();
    const auto opponents = calendar.listOpponents();
    if (!opponents || opponents->empty()) {
        fail("match day: no opponent for a new match");
        return std::nullopt;
    }
    fdss::core::MatchDraft draft;
    draft.opponentId = opponents->front().id;
    draft.date = g_clock->today();
    const auto match = calendar.addMatch(smokeTeam(), draft);
    if (!match) {
        qWarning("qml_smoke: match day step without a match: %s", step);
        fail("match day: cannot add a match for a step");
        return std::nullopt;
    }
    MatchDaySetup setup{match->value, saveMatchDayLineup(match->value)};
    if (setup.players.size() != 14) {
        return std::nullopt;
    }
    if (kickOff) {
        auto& service = ServiceRegistry::instance().matchDay();
        if (!service.start(*match, {2, 45}, fdss::core::kDefaultYellowCardThreshold) ||
            !service.advanceClock(*match)) {
            qWarning("qml_smoke: match day step: %s", step);
            fail("match day: cannot kick off the match of a step");
            return std::nullopt;
        }
    }
    return setup;
}

// User data with markup: the substitute and the opponent (P13 review), shown as plain text
// everywhere, the undo tooltip included. Returns the markup.
QString putMatchDayMarkup(fdss::data::DatabaseManager& database, qint64 matchId,
                          qint64 substitute) {
    const QString markup = QStringLiteral("<b>x</b><img src=\"http://example.invalid/a.png\">");
    QSqlQuery rename(database.database());
    rename.prepare(QStringLiteral("UPDATE player SET last_name = ? WHERE id = ?"));
    rename.addBindValue(markup);
    rename.addBindValue(substitute);
    QSqlQuery renameOpponent(database.database());
    renameOpponent.prepare(QStringLiteral("UPDATE opponent SET name = ? WHERE id = (SELECT "
                                          "opponent_id FROM match WHERE id = ?)"));
    renameOpponent.addBindValue(markup);
    renameOpponent.addBindValue(matchId);
    if (!rename.exec() || !renameOpponent.exec()) {
        fail("match day: cannot put markup into the names");
    }
    return markup;
}

// Each service call of a prepared history must work; the step stops otherwise.
template <class Result> bool prepared(const Result& result, const char* what) {
    if (!result) {
        qWarning("qml_smoke: match day: cannot prepare %s: %s", what,
                 result.error().detail.c_str());
        fail("match day: the history of a step cannot be prepared");
        return false;
    }
    return true;
}

// Start: the dialog of the format, its layout at the minimum window in both languages, Enter
// starts, the hit targets before the kick-off, the kick-off and "Set time" with its undo.
void matchDayStart(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database) {
    using namespace std::chrono_literals;
    const auto setup = newMatchDayMatch("start", false);
    if (!setup) {
        return;
    }
    const qint64 matchId = setup->matchId;
    const std::vector<qint64>& players = setup->players;
    const auto xi = [&](int slot) { return players[static_cast<std::size_t>(slot)]; };
    const qint64 substitute = players[11];
    const QString markup = putMatchDayMarkup(database, matchId, substitute);
    auto screen = openMatchDayScreen(engine, matchId);
    if (!screen) {
        return;
    }
    MatchDayViewModel* matchDay = screen->matchDay;
    if (matchDay->matchId() != matchId || !matchDay->canStart()) {
        fail("match day: the planned match with a lineup cannot start");
        return;
    }
    if (countPlainMarkup(screen->view, markup, "match day before the start") == 0) {
        fail("match day: the opponent with markup is not on the screen");
    }
    (void)xi;
    // Start: the dialog with the half length of a senior team, Enter starts.
    screen->click(QStringLiteral("startButton"));
    auto* startDialog = screen->view->property("startMatchDialog").value<QObject*>();
    if (startDialog == nullptr || !startDialog->property("opened").toBool()) {
        fail("match day: Start does not ask for the half length");
        return;
    }
    renderFrame(screen->window);
    expectPopupHitTargets(startDialog, "start dialog");
    {
        auto* languages = engine.singletonInstance<LanguageManager*>(
            QStringLiteral("FootballDSS"), QStringLiteral("LanguageManager"));
        const QString before = languages->language();
        // At the minimum window, in both languages.
        const QSize size = screen->window->size();
        screen->window->resize(1280, 720);
        for (const QString& language : {QStringLiteral("pl"), QStringLiteral("en")}) {
            expectStartDialogLayout(engine, screen->window, startDialog, language);
        }
        languages->setLanguage(before);
        screen->window->resize(size);
        settle();
        if (startDialog->property("clockless").toBool()) {
            fail("match day: a match of today starts without the clock");
        }
    }
    if (startDialog->property("chosenFormat").toString() != QLatin1StringView("2x45")) {
        fail("match day: the format is not 2 x 45 for a senior team");
    }
    pressKey(screen->window, Qt::Key_Return);
    settle();
    if (matchDay->statusKey() != QLatin1StringView("live") ||
        startDialog->property("visible").toBool()) {
        fail("match day: Enter in the start dialog does not start the match");
        return;
    }
    renderFrame(screen->window);
    expectMatchDayHitTargets(*screen, "before the kick-off");

    screen->click(QStringLiteral("clockButton")); // kick-off
    // "Set time": the dialog, 2:00 typed, then the undo of its toast.
    screen->click(QStringLiteral("setClockButton"));
    if (auto* setClock = screen->view->property("setClockEditor").value<QObject*>();
        setClock == nullptr || !setClock->property("opened").toBool()) {
        fail("match day: Set time does not open its dialog");
    } else {
        renderFrame(screen->window);
        expectPopupHitTargets(setClock, "set time dialog");
        typeText(screen->window, QStringLiteral("2:00"));
        pressKey(screen->window, Qt::Key_Return);
        settle();
        if (setClock->property("visible").toBool() ||
            matchDay->clockText() != QLatin1StringView("02:00")) {
            fail("match day: Set time did not set the clock to 02:00");
        }
        matchDay->undoSetClock(); // the Undo of the toast
        if (matchDay->clockText() != QLatin1StringView("00:00")) {
            fail("match day: the undo of Set time did not bring the clock back");
        }
    }
}

// Events of the first half by taps: a goal with an assist, the accelerators off in a text field, a
// yellow card, the opponent goal with Save, the undo tooltip as plain text, stoppage time, a goal
// entered late at 3' and its undo, a note from its dialog.
void matchDayEvents(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database,
                    const QString& screenshotDirectory) {
    using namespace std::chrono_literals;
    const auto setup = newMatchDayMatch("events", true);
    if (!setup) {
        return;
    }
    const qint64 matchId = setup->matchId;
    const std::vector<qint64>& players = setup->players;
    const auto xi = [&](int slot) { return players[static_cast<std::size_t>(slot)]; };
    const qint64 substitute = players[11];
    const QString markup = putMatchDayMarkup(database, matchId, substitute);
    auto screen = openMatchDayScreen(engine, matchId);
    if (!screen) {
        return;
    }
    MatchDayViewModel* matchDay = screen->matchDay;
    g_clock->advance(10min + 20s);
    screen->click(QStringLiteral("goalButton"));
    screen->tap(QStringLiteral("pitchTile"), xi(9));
    screen->tap(QStringLiteral("pitchTile"), xi(10)); // the assist
    // The accelerators work while nothing else has the focus, and stay off while a text field has
    // it: the letter goes into the field, Ctrl+Z undoes nothing of the match.
    expectShortcutsOff(*screen, false, [] { return true; }, "");
    {
        auto* search =
            findByObjectName(screen->window->contentItem(), QStringLiteral("globalSearch"));
        if (search == nullptr) {
            fail("match day: the search field is not there");
        } else {
            // A text field has the active focus only in the focus window.
            activateWindow(screen->window);
            search->forceActiveFocus();
            settle();
            const int events = matchDay->timeline()->rowCount();
            expectShortcutsOff(
                *screen, true,
                [&] {
                    return matchDay->pendingAction().isEmpty() &&
                           matchDay->timeline()->rowCount() == events;
                },
                "match day: a shortcut works while a text field has the focus");
            search->setProperty("text", QString());
            search->setFocus(false);
            screen->view->forceActiveFocus();
            settle();
        }
    }
    g_clock->advance(20min);
    screen->click(QStringLiteral("yellowButton"));
    screen->tap(QStringLiteral("pitchTile"), xi(3));
    // The opponent goal waits for its minute: Save.
    screen->click(QStringLiteral("opponentGoalButton"));
    screen->click(QStringLiteral("saveActionButton"));
    {
        auto* undoButton = screen->find(QStringLiteral("undoButton"));
        auto* tip = undoButton != nullptr
                        ? undoButton->findChild<QObject*>(QStringLiteral("undoToolTip"))
                        : nullptr;
        auto* tipText =
            tip != nullptr ? tip->property("contentItem").value<QQuickItem*>() : nullptr;
        if (tip == nullptr || tipText == nullptr ||
            !tip->property("text").toString().contains(markup) ||
            tipText->property("textFormat").toInt() != Qt::PlainText) {
            fail("match day: the undo tooltip does not show the opponent as plain text");
        }
    }
    g_clock->advance(16min); // 46:20: stoppage time
    renderFrame(screen->window);
    if (!matchDay->clockText().startsWith(QLatin1StringView("45+1:"))) {
        fail("match day: the clock does not show the stoppage time as 45+1");
    }
    expectMatchDayHitTargets(*screen, "first half");
    if (!screenshotDirectory.isEmpty()) {
        saveScreenshot(screen->window, screenshotDirectory, QStringLiteral("match-day.png"));
    }
    // A goal entered late at 3': the minute field of the action, typed over.
    screen->click(QStringLiteral("goalButton"));
    if (!screenshotDirectory.isEmpty()) {
        renderFrame(screen->window);
        saveScreenshot(screen->window, screenshotDirectory, QStringLiteral("match-day-minute.png"));
    }
    if (auto* minute = screen->find(QStringLiteral("minuteField"))) {
        minute->forceActiveFocus();
        QMetaObject::invokeMethod(minute, "selectAll");
        typeText(screen->window, QStringLiteral("3"));
        if (matchDay->pendingMinute() != QLatin1StringView("3") ||
            !matchDay->pendingMinuteError().isEmpty()) {
            fail("match day: the typed minute does not reach the action");
        }
    } else {
        fail("match day: the minute field is not there");
    }
    screen->tap(QStringLiteral("pitchTile"), xi(10));
    screen->click(QStringLiteral("noAssistButton"));
    if (matchDay->goalsFor() != 2 ||
        matchDay->timeline()->value(matchDay->timeline()->rowCount() - 1, "minuteText") !=
            QStringLiteral("3'")) {
        fail("match day: the goal entered late is not at 3'");
    }
    // Undo takes back that goal, the action entered last, whatever its minute.
    screen->click(QStringLiteral("undoButton"));
    if (matchDay->goalsFor() != 1) {
        fail("match day: undo did not take back the goal entered late");
    }
    // A note from its dialog: Enter saves it with the last minute played.
    screen->click(QStringLiteral("noteButton"));
    auto* noteDialog = screen->view->property("noteEditor").value<QObject*>();
    if (noteDialog == nullptr || !noteDialog->property("opened").toBool()) {
        fail("match day: Note does not open its dialog");
    } else {
        renderFrame(screen->window);
        expectPopupHitTargets(noteDialog, "note dialog");
        typeText(screen->window, QStringLiteral("Wiatr"));
        // Enter from the minute field saves once.
        noteDialog->property("minuteInput").value<QQuickItem*>()->forceActiveFocus();
        pressKey(screen->window, Qt::Key_Return);
        settle();
        if (noteDialog->property("visible").toBool() ||
            matchDay->timeline()->value(0, "kind") != QStringLiteral("note")) {
            fail("match day: the note was not saved on the timeline");
        }
    }
}

// A substitution in the half-time break by taps (the substitute's markup as plain text), the second
// half, and the leave guard with Stay. Prepared: the first half kicked off, 46:20 on the clock.
void matchDayBreakAndLeave(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database) {
    using namespace std::chrono_literals;
    const auto setup = newMatchDayMatch("break", true);
    if (!setup) {
        return;
    }
    const qint64 matchId = setup->matchId;
    const std::vector<qint64>& players = setup->players;
    const auto xi = [&](int slot) { return players[static_cast<std::size_t>(slot)]; };
    const qint64 substitute = players[11];
    const QString markup = putMatchDayMarkup(database, matchId, substitute);
    g_clock->advance(46min + 20s);
    auto screen = openMatchDayScreen(engine, matchId);
    if (!screen) {
        return;
    }
    MatchDayViewModel* matchDay = screen->matchDay;
    screen->click(QStringLiteral("clockButton")); // half time
    // A substitution in the break.
    screen->click(QStringLiteral("subButton"));
    screen->tap(QStringLiteral("pitchTile"), xi(4));
    screen->tap(QStringLiteral("benchTile"), substitute);
    renderFrame(screen->window);
    // The tile and the timeline name the substitute with markup.
    if (countPlainMarkup(screen->view, markup, "match day after the substitution") < 2) {
        fail("match day: the substitute with markup is not on the tile and in the timeline");
    }
    g_clock->advance(15min);
    screen->click(QStringLiteral("clockButton")); // second half

    // Leaving the live match asks; Stay stays.
    expectFlag(screen->navigation, "leaveGuard", true, "a live match does not guard leaving");
    screen->navigation->activate(QStringLiteral("dashboard"));
    settle();
    auto* question = screen->view->property("leaveDialog").value<QObject*>();
    if (question == nullptr || !question->property("visible").toBool() ||
        screen->navigation->currentKey() != QLatin1StringView("matchday")) {
        fail("match day: leaving the live match does not ask");
    } else {
        renderFrame(screen->window);
        expectPopupHitTargets(question, "leave dialog");
        const int events = matchDay->timeline()->rowCount();
        expectShortcutsOff(
            *screen, true,
            [&] {
                return matchDay->pendingAction().isEmpty() &&
                       matchDay->timeline()->rowCount() == events;
            },
            "match day: a shortcut works while a dialog is open");
        QMetaObject::invokeMethod(question, "reject");
        settle();
        if (screen->navigation->currentKey() != QLatin1StringView("matchday")) {
            fail("match day: Stay leaves the live match");
        }
    }
}

// The whole match to its end, from 60:00: a second yellow, a goal by the substitute, its undo and
// again, the end, the score, the timeline and the minutes against a hand count, the full screen.
// Prepared through the service as the events before 60' happened on the screen: a goal at 10' with
// an assist, a yellow and the opponent goal at 30', a note in stoppage time, the half time, the
// substitution in the break and the second half.
void exerciseMatchReport(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database,
                         qint64 matchId, const std::vector<qint64>& players);

void matchDayFullMatch(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database) {
    using namespace std::chrono_literals;
    const auto setup = newMatchDayMatch("full match", true);
    if (!setup) {
        return;
    }
    const qint64 matchId = setup->matchId;
    const std::vector<qint64>& players = setup->players;
    const auto xi = [&](int slot) { return players[static_cast<std::size_t>(slot)]; };
    const qint64 substitute = players[11];
    {
        auto& service = ServiceRegistry::instance().matchDay();
        const fdss::core::MatchId match{matchId};
        g_clock->advance(10min + 20s);
        if (!prepared(service.recordGoal(match, fdss::core::PlayerId{xi(9)},
                                         fdss::core::PlayerId{xi(10)}),
                      "the goal")) {
            return;
        }
        g_clock->advance(20min);
        if (!prepared(service.recordYellow(match, fdss::core::PlayerId{xi(3)}), "the yellow") ||
            !prepared(service.recordOpponentGoal(match), "the opponent goal")) {
            return;
        }
        g_clock->advance(16min);
        if (!prepared(service.addNote(match, std::nullopt, std::nullopt, "Wiatr"), "the note") ||
            !prepared(service.advanceClock(match), "the half time") ||
            !prepared(service.recordSubstitution(match, fdss::core::PlayerId{substitute},
                                                 fdss::core::PlayerId{xi(4)}),
                      "the substitution")) {
            return;
        }
        g_clock->advance(15min);
        if (!prepared(service.advanceClock(match), "the second half")) {
            return;
        }
    }
    auto screen = openMatchDayScreen(engine, matchId);
    if (!screen) {
        return;
    }
    MatchDayViewModel* matchDay = screen->matchDay;
    g_clock->advance(15min); // 60:00
    screen->click(QStringLiteral("yellowButton"));
    screen->tap(QStringLiteral("pitchTile"), xi(3)); // the second yellow: sent off
    g_clock->advance(20min);                         // 80:00
    screen->click(QStringLiteral("goalButton"));
    screen->tap(QStringLiteral("pitchTile"), substitute);
    screen->click(QStringLiteral("noAssistButton"));
    // Undo takes the goal back; record it again.
    screen->click(QStringLiteral("undoButton"));
    if (matchDay->goalsFor() != 1) {
        fail("match day: Undo does not take the last goal back");
    }
    screen->click(QStringLiteral("goalButton"));
    screen->tap(QStringLiteral("pitchTile"), substitute);
    screen->click(QStringLiteral("noAssistButton"));
    g_clock->advance(12min);
    screen->click(QStringLiteral("clockButton")); // the end
    if (matchDay->statusKey() != QLatin1StringView("finished") || matchDay->goalsFor() != 2 ||
        matchDay->goalsAgainst() != 1 || matchDay->timeline()->rowCount() != 8) {
        fail("match day: the finished match does not show 2:1, its seven events and the note");
    }
    expectFlag(screen->navigation, "leaveGuard", false, "a finished match guards leaving");
    if (screen->navigation->currentKey() != QLatin1StringView("matchreport")) {
        fail("match day: the end of the match does not open the report");
    }

    // The hand count: XI 0–90, the player taken off at half time 0–45, the substitute 45–90, the
    // player sent off at 60 0–60.
    const auto expectPlayed = [&](qint64 player, int on, int off) {
        if (smokeMinutes(database, matchId, player, "minute_on") != on ||
            smokeMinutes(database, matchId, player, "minute_off") != off) {
            qWarning("qml_smoke: match day: player %lld is not %d–%d", player, on, off);
            fail("match day: the minutes in the database differ from the hand count");
        }
    };
    expectPlayed(xi(0), 0, 90);
    expectPlayed(xi(9), 0, 90);
    expectPlayed(xi(4), 0, 45);
    expectPlayed(substitute, 45, 90);
    expectPlayed(xi(3), 0, 60);
    if (smokeMinutes(database, matchId, players[12], "minute_on") != -1) {
        fail("match day: an unused substitute has minutes");
    }

    exerciseMatchReport(engine, database, matchId, players);
    screen = openMatchDayScreen(engine, matchId);
    if (!screen) {
        return;
    }

    // Full screen on and off.
    screen->click(QStringLiteral("fullScreenButton"));
    expectFlag(screen->view, "fullScreen", true, "match day: full screen does not turn on");
    screen->click(QStringLiteral("fullScreenButton"));
    expectFlag(screen->view, "fullScreen", false, "match day: full screen does not turn off");
}

// The report after the end of the match (packet P14, Done when): every player who played rated by
// the keyboard (a digit, Tab to the RPE, a digit, Tab to the next row), Ctrl+S, the ratings in the
// database and on the "Matches" tab of the player card; leaving with changes asks; a forgotten
// substitution added afterwards changes the minutes.
void exerciseMatchReport(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database,
                         qint64 matchId, const std::vector<qint64>& players) {
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    QQuickItem* view = findQmlItem(window->contentItem(), "MatchReportView");
    if (view == nullptr) {
        fail("match report: the view is not loaded");
        return;
    }
    auto* report = view->property("viewModel").value<MatchReportViewModel*>();
    if (report == nullptr || report->matchId() != matchId || report->readOnly()) {
        fail("match report: the finished match is not open for the coach");
        return;
    }
    const int rows = report->ratings()->rowCount();
    if (rows != 12) { // the XI and the substitute who came on
        qWarning("qml_smoke: match report: %d rows", rows);
        fail("match report: the grid does not hold every player who played");
        return;
    }
    renderFrame(window);
    // The focus comes into the first rating with the report.
    QQuickItem* first = findByObjectName(view, QStringLiteral("ratingCell_0"));
    if (first == nullptr || !first->hasActiveFocus()) {
        if (first != nullptr) {
            first->forceActiveFocus(Qt::TabFocusReason);
        }
        if (first == nullptr || !first->hasActiveFocus()) {
            fail("match report: the first rating has no focus");
            return;
        }
    }
    for (int row = 0; row < rows; ++row) {
        pressDigit(window, 7);
        pressKey(window, Qt::Key_Tab);
        pressDigit(window, row == 0 ? 1 : 6);
        if (row == 0) {
            pressDigit(window, 0); // 1 then 0: 10
        }
        if (row + 1 < rows) {
            pressKey(window, Qt::Key_Tab);
        }
    }
    expectFlag(report, "dirty", true, "match report: the ratings typed are not a change");
    expectFlag(navigation, "leaveGuard", true, "match report: changes do not guard leaving");
    // Leaving asks; Stay keeps the changes.
    navigation->activate(QStringLiteral("squad"));
    settle();
    auto* leave = view->property("leaveDialog").value<QObject*>();
    expectFlag(leave, "visible", true, "match report: leaving with changes does not ask");
    QMetaObject::invokeMethod(leave, "reject");
    settle();
    if (navigation->currentKey() != QLatin1StringView("matchreport")) {
        fail("match report: Stay does not stay");
    }
    if (QQuickItem* last =
            findByObjectName(view, QStringLiteral("matchRpeCell_%1").arg(rows - 1))) {
        last->forceActiveFocus(Qt::TabFocusReason);
    }
    QKeyEvent press(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier, QStringLiteral("s"));
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_S, Qt::ControlModifier, QStringLiteral("s"));
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    settle();
    expectFlag(report, "dirty", false, "match report: Ctrl+S does not save");
    QSqlQuery rated(database.database());
    rated.prepare(QStringLiteral("SELECT count(*), sum(rating = 10), sum(rpe = 10) FROM appearance "
                                 "WHERE match_id = ? AND rating IS NOT NULL AND rpe IS NOT NULL"));
    rated.addBindValue(matchId);
    if (!rated.exec() || !rated.next() || rated.value(0).toInt() != rows ||
        rated.value(1).toInt() != 0 || rated.value(2).toInt() != 1) {
        fail("match report: the ratings in the database are not those typed");
    }

    // The "Matches" tab of the card of the first player rated.
    const qint64 ratedPlayer = report->ratings()->value(0, "playerId").toLongLong();
    navigation->activate(QStringLiteral("squad"), {{QStringLiteral("playerId"), ratedPlayer}});
    settle();
    QQuickItem* card = findQmlItem(window->contentItem(), "PlayerCardView");
    auto* matches =
        card != nullptr ? card->property("matchesModel").value<PlayerMatchesModel*>() : nullptr;
    if (matches == nullptr || matches->count() < 1 ||
        matches->value(0, "matchId").toLongLong() != matchId ||
        matches->value(0, "rating").toInt() != 7 || matches->value(0, "rpe").toInt() != 10) {
        fail("match report: the player card does not show the rating of the match");
        return;
    }
    // A row opens the report again.
    QMetaObject::invokeMethod(card, "openMatchReport", Q_ARG(QVariant, double(matchId)));
    settle();
    if (navigation->currentKey() != QLatin1StringView("matchreport")) {
        fail("match report: the Matches tab does not open the report");
    }

    // A forgotten substitution: the last bench player for the goalkeeper's neighbour at 70'.
    const qint64 comingOn = players[13];
    const qint64 goingOff = players[1];
    if (!report->addEvent(QStringLiteral("sub"), QStringLiteral("71"), comingOn, goingOff)) {
        fail("match report: the forgotten substitution cannot be added");
        return;
    }
    if (smokeMinutes(database, matchId, goingOff, "minute_off") != 70 ||
        smokeMinutes(database, matchId, comingOn, "minute_on") != 70 ||
        smokeMinutes(database, matchId, comingOn, "minute_off") != 90) {
        fail("match report: the forgotten substitution does not change the minutes");
    }
    // The ratings stay.
    QSqlQuery kept(database.database());
    kept.prepare(
        QStringLiteral("SELECT rating FROM appearance WHERE match_id = ? AND player_id = ?"));
    kept.addBindValue(matchId);
    kept.addBindValue(goingOff);
    if (!kept.exec() || !kept.next() || kept.value(0).toInt() != 7) {
        fail("match report: a correction lost a rating");
    }

    // The dialog offers the players of the match as it is now (P14 review): open, cancel, delete
    // the substitution, open again.
    auto* dialog = view->property("correctionEditor").value<QObject*>();
    if (dialog == nullptr) {
        fail("match report: the correction dialog is not there");
        return;
    }
    const auto offers = [&](qint64 player) {
        for (const QVariant& option : dialog->property("firstOptions").toList()) {
            if (option.toMap().value(QStringLiteral("value")).toLongLong() == player) {
                return true;
            }
        }
        return false;
    };
    QMetaObject::invokeMethod(dialog, "openForAdding");
    settle();
    expectFlag(dialog, "visible", true, "match report: Add event does not open the dialog");
    if (!offers(comingOn) || offers(goingOff)) {
        fail("match report: the dialog does not offer the players on the pitch at the end");
    }
    QMetaObject::invokeMethod(dialog, "reject");
    settle();
    qint64 substitution = 0;
    for (const auto& event :
         ServiceRegistry::instance().matchReport().report(fdss::core::MatchId{matchId})->events) {
        if (event.type == fdss::core::MatchEventType::Sub &&
            event.playerId == fdss::core::PlayerId{comingOn}) {
            substitution = event.id.value;
        }
    }
    if (substitution == 0 || !report->deleteEvent(substitution)) {
        fail("match report: the forgotten substitution cannot be deleted");
        return;
    }
    QMetaObject::invokeMethod(dialog, "openForAdding");
    settle();
    if (offers(comingOn) || !offers(goingOff)) {
        fail("match report: the dialog offers the players from before the correction");
    }
    // Open, the lists follow a correction made meanwhile (Undo of the delete).
    report->undoDelete();
    settle();
    if (!offers(comingOn) || offers(goingOff)) {
        fail("match report: the open dialog does not follow a correction");
    }
    QMetaObject::invokeMethod(dialog, "reject");
    settle();
}

// Another role logs in during a live match: Match Day opens on it, read-only, and leaving does not
// ask. Last: it logs in another user (and the coach again).
void matchDayReadOnly(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database) {
    const fdss::core::StaffUserId coach = ServiceRegistry::instance().session().user()->id;
    using namespace std::chrono_literals;
    const auto setup = newMatchDayMatch("read-only", true);
    if (!setup) {
        return;
    }
    const qint64 matchId = setup->matchId;
    const std::vector<qint64>& players = setup->players;
    const auto xi = [&](int slot) { return players[static_cast<std::size_t>(slot)]; };
    const qint64 substitute = players[11];
    auto screen = openMatchDayScreen(engine, matchId);
    if (!screen) {
        return;
    }
    MatchDayViewModel* matchDay = screen->matchDay;
    (void)xi;
    (void)substitute;
    (void)matchDay;
    // Another role logs in: Match Day opens on the live match, read-only.
    const auto physio = addSmokeStaff("Fizjo Mecz", fdss::core::Role::Physio);
    if (!physio || !logInAgain(engine, *physio)) {
        return;
    }
    if (screen->navigation->currentKey() != QLatin1StringView("matchday")) {
        fail("match day: a login does not open the live match");
    }
    screen = openMatchDayScreen(engine);
    if (!screen) {
        return;
    }
    if (screen->matchDay->matchId() != matchId || !screen->matchDay->readOnly()) {
        fail("match day: another role does not see the live match read-only");
    }
    expectFlag(screen->find(QStringLiteral("actionRow")), "visible", false,
               "match day: a role without RunMatchDay sees the action buttons");
    expectFlag(screen->navigation, "leaveGuard", false, "a read-only live match guards leaving");
    if (!logInAgain(engine, coach)) {
        return;
    }
    screen = openMatchDayScreen(engine);
    if (!screen) {
        return;
    }
}

// Live tactics (P13b) on a match of its own, kicked off and at 30:20.
void matchDayLiveTactics(QQmlApplicationEngine& engine, const QString& screenshotDirectory) {
    using namespace std::chrono_literals;
    const auto setup = newMatchDayMatch("tactics", true);
    if (!setup) {
        return;
    }
    g_clock->advance(30min + 20s);
    const auto screen = openMatchDayScreen(engine, setup->matchId);
    if (!screen) {
        return;
    }
    exerciseLiveTactics(engine, *screen, screenshotDirectory);
}

void exerciseMatchDay(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database,
                      const QString& screenshotDirectory) {
    if (!ServiceRegistry::instance().demoData().load(smokeTeam())) {
        fail("cannot load the demo club for match day");
        return;
    }
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        fail("Main.qml did not load");
        return;
    }
    settle();
    matchDayStart(engine, database);
    matchDayEvents(engine, database, screenshotDirectory);
    matchDayLiveTactics(engine, screenshotDirectory);
    matchDayBreakAndLeave(engine, database);
    matchDayFullMatch(engine, database);
    // Last: it logs in another user.
    matchDayReadOnly(engine, database);
}

} // namespace

// Main.qml on the summary step of the wizard, with the club and the Coach filled in. Null when it
// is not there; the failure is already reported.
QQuickWindow* openWizardSummary(QQmlApplicationEngine& engine) {
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    settle();
    if (engine.rootObjects().isEmpty()) {
        fail("Main.qml did not load for the demo setup");
        return nullptr;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* wizard = wizardOf(window);
    if (wizard == nullptr) {
        fail("the wizard is not there for the demo setup");
        return nullptr;
    }
    wizard->setClubName(QStringLiteral("KS Demo"));
    wizard->setTeamName(QStringLiteral("Seniorzy"));
    wizard->setCoachName(QStringLiteral("Jan Trener"));
    wizard->setPin(QStringLiteral("1234"));
    wizard->setPinRepeat(QStringLiteral("1234"));
    for (int step = 0; step < 3; ++step) {
        if (!wizard->next()) {
            fail("the wizard did not reach the summary for the demo setup");
            return nullptr;
        }
    }
    settle();
    window->grabWindow(); // the summary step is laid out on the way to a frame
    return window;
}

// The first run with the demo: "Load demo data" picked with the mouse on the summary step, Enter
// creates the club, and the squad of the new club has its 22 active players.
void exerciseDemoSetup(QQmlApplicationEngine& engine) {
    QQuickWindow* window = openWizardSummary(engine);
    if (window == nullptr) {
        return;
    }
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    auto* wizard = wizardOf(window);
    auto* view = window->property("setupWizard").value<QQuickItem*>();
    QQuickItem* choice =
        view != nullptr ? findByObjectName(view, QStringLiteral("demoChoice")) : nullptr;
    QQuickItem* demoSegment =
        choice != nullptr ? findByObjectName(choice, QStringLiteral("segment1")) : nullptr;
    if (demoSegment == nullptr) {
        fail("the demo choice is not on the summary step");
        return;
    }
    clickItem(window, demoSegment);
    if (!wizard->loadDemoData()) {
        fail("a click on Load demo data did not choose the demo");
        return;
    }
    if (wizard->category() != QStringLiteral("senior")) {
        fail("the demo did not show the senior category");
    }

    pressKey(window, Qt::Key_Return);
    if (!waitUntil([&] { return appState->state() == AppState::State::Ready; },
                   "Enter on the summary did not create the demo club")) {
        return;
    }
    const auto context = ServiceRegistry::instance().setup().clubContext();
    if (!context) {
        fail("the demo club is not there");
        return;
    }
    const auto players = ServiceRegistry::instance().squad().listPlayers(context->team.id);
    if (!players) {
        fail("the squad of the demo club cannot be read");
        return;
    }
    const auto active = std::ranges::count_if(
        *players, [](const auto& entry) { return !entry.player.archivedAt.has_value(); });
    if (active != 22) {
        fail("the demo club does not have 22 active players");
    }
}

// A demo that fails at its last row: the login after the setup dismisses every toast, and the
// message about the empty club still has to be there once the shell is.
void exerciseFailedDemoSetup(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database) {
    if (const auto broken = database.execSql(
            QStringLiteral(
                "CREATE TEMP TRIGGER fail_demo BEFORE INSERT ON staff_user "
                "WHEN NEW.role = 'physio' BEGIN SELECT RAISE(ABORT, 'demo failure'); END"),
            "break the demo");
        !broken) {
        fail("the demo cannot be made to fail");
        return;
    }
    QQuickWindow* window = openWizardSummary(engine);
    if (window == nullptr) {
        return;
    }
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    auto* toasts =
        engine.singletonInstance<QObject*>(QStringLiteral("FootballDSS"), QStringLiteral("Toasts"));
    wizardOf(window)->setLoadDemoData(true);
    settle();
    {
        const std::lock_guard lock(g_messagesMutex);
        g_expectedCritical = "demo failure";
        g_expectedCriticalCount = 0;
    }
    pressKey(window, Qt::Key_Return);
    const bool ready = waitUntil([&] { return appState->state() == AppState::State::Ready; },
                                 "Enter on the summary did not create the club");
    settle();
    {
        const std::lock_guard lock(g_messagesMutex);
        g_expectedCritical = nullptr;
        if (g_expectedCriticalCount != 1) {
            fail("the failed demo was not logged exactly once");
        }
    }
    if (!ready) {
        return;
    }
    if (window->property("shell").value<QQuickItem*>() == nullptr) {
        fail("the shell is not there after the failed demo");
    }
    const QString expected =
        QCoreApplication::translate("fdss::viewmodels::SetupWizardViewModel",
                                    "Demo data could not be loaded. The club starts empty.");
    if (!toasts->property("active").toBool() || toasts->property("text").toString() != expected) {
        fail("the failed demo is not said once the shell is there");
    }
}

// Packet P11: the Load screen. The computation runs on the thread pool, so every step waits for it.
struct LoadScreen {
    QQuickWindow* window = nullptr;
    NavigationModel* navigation = nullptr;
    QQuickItem* view = nullptr;
    LoadViewModel* load = nullptr;

    [[nodiscard]] QQuickItem* find(const QString& name) const {
        return findByObjectName(view, name);
    }
};

// Runs `start`, which must start a read of the team load, while the results are held: the read is
// still running when `during` looks at the screen, whatever the speed of the runner (CI review of
// P11: a result ready before the viewmodel attached its continuation came in inside the call that
// started the read). Then the read is let go and waited for.
template <class Start, class During>
bool readLoad(LoadViewModel* load, Start start, During during, const char* what) {
    fdss::services::LoadService& service = ServiceRegistry::instance().load();
    service.holdResults();
    start();
    const QByteArray notStarted = what + QByteArrayLiteral(": no read of the load started");
    const bool started = waitUntil([load] { return load->busy(); }, notStarted.constData());
    if (started) {
        settle();
        during();
    }
    service.releaseResults();
    const QByteArray notDone = what + QByteArrayLiteral(": the read of the load did not finish");
    if (!started || !waitUntil([load] { return !load->busy(); }, notDone.constData())) {
        return false;
    }
    settle();
    return true;
}

// Goes to the section (again, so it reads anew) and waits for that read.
std::optional<LoadScreen> openLoadScreen(QQmlApplicationEngine& engine) {
    LoadScreen screen;
    screen.window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    screen.navigation = screen.window->property("navigation").value<NavigationModel*>();
    // Every section is loaded with the shell, the hidden ones too.
    screen.view = findQmlItem(screen.window->contentItem(), "LoadView");
    if (screen.view == nullptr) {
        fail("the load view is not loaded");
        return std::nullopt;
    }
    screen.load = screen.view->property("viewModel").value<LoadViewModel*>();
    if (screen.load == nullptr) {
        fail("the load view has no viewmodel");
        return std::nullopt;
    }
    const bool read = readLoad(
        screen.load,
        [&screen] {
            screen.navigation->activate(QStringLiteral("dashboard"));
            settle();
            screen.navigation->activate(QStringLiteral("load"));
        },
        [] {}, "opening the load screen");
    if (!read) {
        return std::nullopt;
    }
    return screen;
}

PlayerLoadViewModel* waitForPlayerLoad(QQuickItem* panel, const char* reason) {
    auto* player =
        panel != nullptr ? panel->property("viewModel").value<PlayerLoadViewModel*>() : nullptr;
    if (player == nullptr) {
        fail(reason);
        return nullptr;
    }
    if (!waitUntil([player] { return !player->busy(); }, reason)) {
        return nullptr;
    }
    settle();
    return player;
}

// The language a load scenario starts in: the one an earlier scenario left (P11 review: the
// checks must not depend on it), or the default of a run of the load scenarios alone (nullopt).
void startLoadLanguage(QQmlApplicationEngine& engine, const std::optional<QString>& language) {
    if (!language) {
        return;
    }
    auto* languages = engine.singletonInstance<LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));
    languages->setLanguage(*language);
    settle();
}

// Empty club: the table says what to do; then three days of RPE: grey with a hint, no green.
void exerciseLoadNewClub(QQmlApplicationEngine& engine, const std::optional<QString>& language,
                         const QString& screenshotDirectory) {
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        fail("Main.qml did not load");
        return;
    }
    settle();
    startLoadLanguage(engine, language);
    auto screen = openLoadScreen(engine);
    if (!screen) {
        return;
    }
    if (screen->load->playerCount() != 0) {
        fail("an empty club shows players on the load screen");
    }
    grabOrSave(screen->window, screenshotDirectory, QStringLiteral("load-empty.png"));

    const fdss::core::TeamId team = smokeTeam();
    std::vector<fdss::core::PlayerId> players;
    for (const auto& [first, last, number] :
         {std::tuple{"Jan", "Kowalski", 7}, std::tuple{"Adam", "Nowak", 9}}) {
        fdss::core::PlayerDraft draft;
        draft.firstName = first;
        draft.lastName = last;
        draft.shirtNumber = number;
        const auto id = ServiceRegistry::instance().squad().addPlayer(team, draft);
        if (!id) {
            fail("cannot add a player for the load screen");
            return;
        }
        players.push_back(*id);
    }
    for (const int ago : {5, 3, 1}) {
        fdss::core::TrainingSessionDraft draft;
        draft.date =
            fdss::core::Date{std::chrono::sys_days{g_clock->today()} - std::chrono::days{ago}};
        const auto session = ServiceRegistry::instance().calendar().addSession(team, draft);
        std::vector<fdss::core::AttendanceDraft> rows;
        for (const fdss::core::PlayerId player : players) {
            fdss::core::AttendanceDraft row;
            row.playerId = player;
            row.rpe = 6;
            rows.push_back(row);
        }
        if (!session || !ServiceRegistry::instance().training().saveAttendance(*session, rows)) {
            fail("cannot save RPE for the load screen");
            return;
        }
    }
    screen = openLoadScreen(engine);
    if (!screen) {
        return;
    }
    if (screen->load->playerCount() != 2) {
        fail("the load screen does not show the two players");
    }
    for (int row = 0; row < screen->load->players()->rowCount(); ++row) {
        if (screen->load->rowAt(row).value(QStringLiteral("statusKey")).toString() !=
            QLatin1StringView("notEnoughData")) {
            fail("three days of RPE give a status other than not enough data");
        }
    }
    expectFlag(screen->find(QStringLiteral("teamHint")), "visible", true,
               "three days of RPE, and no hint what to enter");
    auto* panel = screen->find(QStringLiteral("loadPlayerPanel"));
    if (auto* player = waitForPlayerLoad(panel, "the panel of a new player did not load")) {
        if (!player->hasData() || player->baselineReady()) {
            fail("the panel of a new player is not acute only");
        }
        expectFlag(findByObjectName(panel, QStringLiteral("ratioChartView")), "visible", false,
                   "the ratio panel is shown without a baseline");
    }
    grabOrSave(screen->window, screenshotDirectory, QStringLiteral("load-new-club.png"));
}

// P11 review and CI: at 1280×720 with the sidebar expanded, the status cell holds the longest short
// status of the language with the widest day beside it, in the font of the machine and in a wider
// one: the column is measured in the font (a width in pixels did not fit the fonts of CI). Checked
// in a real cell of the table, whose badge is given each text in turn; a read then restores the
// bindings.
void expectLoadStatusesFit(QQmlApplicationEngine& engine, LoadScreen& screen) {
    auto* languages = engine.singletonInstance<LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));
    const QString startLanguage = languages->language();
    if (screen.window->property("sidebarCollapsed").toBool()) {
        fail("the sidebar is collapsed for the status width check");
    }
    const auto startFont = screen.window->property("font").value<QFont>();
    QFont wideFont = startFont;
    constexpr qreal kWiderLetters = 2;
    wideFont.setLetterSpacing(QFont::AbsoluteSpacing, kWiderLetters);

    const auto fits = [&screen](const QString& language, const QFont& font) {
        auto* badge = screen.find(QStringLiteral("loadStatus_0"));
        QQuickItem* cell = badge != nullptr ? badge->parentItem() : nullptr;
        auto* note =
            badge != nullptr ? findByObjectName(badge, QStringLiteral("loadStatusNote")) : nullptr;
        if (cell == nullptr || note == nullptr) {
            fail("the load table has no status cell");
            return false;
        }
        // The widest day in the font of the cell.
        const QFontMetricsF metrics(note->property("font").value<QFont>());
        const QStringList days = screen.load->asOfShortSamples();
        const auto widestDay = std::ranges::max_element(
            days, {}, [&metrics](const QString& day) { return metrics.horizontalAdvance(day); });
        if (widestDay == days.end()) {
            fail("the load screen has no days to measure");
            return false;
        }
        bool allFit = true;
        for (const auto status :
             {fdss::core::LoadStatus::High, fdss::core::LoadStatus::Elevated,
              fdss::core::LoadStatus::Normal, fdss::core::LoadStatus::Underloaded,
              fdss::core::LoadStatus::NotEnoughData}) {
            badge->setProperty("statusKey", fdss::viewmodels::LoadTexts::statusKey(status));
            badge->setProperty("text", fdss::viewmodels::LoadTexts::statusShortName(status));
            badge->setProperty("note", *widestDay);
            // A frame lays the rows of the badge out (polish); events alone may not.
            renderFrame(screen.window);
            const qreal right = badge->mapToItem(cell, QPointF(badge->implicitWidth(), 0)).x();
            if (badge->implicitWidth() > cell->width() || right > cell->width()) {
                qWarning("qml_smoke: status '%s' with the day '%s' (%s, letter spacing %.0f) needs "
                         "%.0f px, the cell has %.0f",
                         qUtf8Printable(badge->property("text").toString()),
                         qUtf8Printable(*widestDay), qUtf8Printable(language), font.letterSpacing(),
                         badge->implicitWidth(), cell->width());
                allFit = false;
            }
        }
        return allFit;
    };
    bool allFit = true;
    for (const QString& language : {QStringLiteral("pl"), QStringLiteral("en")}) {
        for (const QFont& font : {startFont, wideFont}) {
            languages->setLanguage(language);
            screen.window->setProperty("font", font);
            settle();
            renderFrame(screen.window);
            allFit = fits(language, font) && allFit;
        }
    }
    if (!allFit) {
        fail("a load status does not fit its column at the minimum window");
    }
    languages->setLanguage(startLanguage);
    screen.window->setProperty("font", startFont);
    settle();
    readLoad(screen.load, [&screen] { screen.load->reload(); }, [] {}, "after the width check");
}

// P11 review: navigationArgs.playerId arrives while the table is read, for a player the status
// filter hides: the filter is cleared and that player selected after the read, not the first row.
// The read is held while the screen is checked, so nothing depends on the speed of the runner.
void exerciseLoadNavigation(QQmlApplicationEngine& engine, LoadScreen& screen) {
    LoadViewModel* load = screen.load;
    // Rows beyond the visible ones are created asynchronously, and offscreen only frames move that
    // on: finish it, so the filter below does not cancel a row that is no longer there (a Qt
    // warning).
    if (QQmlIncubationController* incubation = engine.incubationController()) {
        incubation->incubateFor(1000);
    }
    settle();
    qint64 requested = 0;
    for (int row = load->players()->rowCount() - 1; row >= 0 && requested == 0; --row) {
        if (load->rowAt(row).value(QStringLiteral("statusKey")).toString() !=
            QLatin1StringView("high")) {
            requested = load->rowAt(row).value(QStringLiteral("playerId")).toLongLong();
        }
    }
    load->setStatusFilter(QStringLiteral("high"));
    settle();
    if (requested == 0 || load->rowOf(requested) >= 0) {
        fail("the navigation check has no player hidden by the filter");
        return;
    }
    const auto selected = [&screen] {
        return screen.view->property("selectedPlayer").toMap().value(QStringLiteral("playerId"));
    };
    const bool read = readLoad(
        load,
        [&screen, requested] {
            screen.navigation->activate(QStringLiteral("dashboard"));
            settle();
            screen.navigation->activate(QStringLiteral("load"),
                                        QVariantMap{{QStringLiteral("playerId"), requested}});
        },
        [load, requested, &selected] {
            // During the read the request waits: the old rows know nothing of it.
            if (load->statusFilter() != QLatin1StringView("high") ||
                selected().toLongLong() == requested) {
                fail("navigationArgs.playerId was applied before the read finished");
            }
        },
        "the navigation to a player");
    if (!read) {
        return;
    }
    if (load->statusFilter() != QLatin1StringView("all")) {
        fail("a requested player hidden by the filter did not clear it");
    }
    if (selected() != requested) {
        fail("navigationArgs.playerId did not select that player after the read");
    }
    auto* panel = screen.find(QStringLiteral("loadPlayerPanel"));
    if (auto* player = waitForPlayerLoad(panel, "the panel of the requested player did not load")) {
        if (player->playerId() != requested) {
            fail("the panel does not show the requested player");
        }
    }
}

// Done when: the demo club shows one red player with a chart that explains why, on the Load screen
// and on the player card, in both languages and themes and at the minimum window.
void exerciseLoadDemo(QQmlApplicationEngine& engine, const std::optional<QString>& language,
                      const QString& screenshotDirectory) {
    if (!ServiceRegistry::instance().demoData().load(smokeTeam())) {
        fail("cannot load the demo club for the load screen");
        return;
    }
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        fail("Main.qml did not load");
        return;
    }
    settle();
    startLoadLanguage(engine, language);
    auto screen = openLoadScreen(engine);
    if (!screen) {
        return;
    }
    LoadViewModel* load = screen->load;
    if (load->playerCount() != fdss::core::demo::kActivePlayers) {
        fail("the load screen does not show the active players of the demo club");
        return;
    }
    // Sorted by status: the red player first, selected, and nobody grey.
    int red = 0;
    for (int row = 0; row < load->players()->rowCount(); ++row) {
        const QString key = load->rowAt(row).value(QStringLiteral("statusKey")).toString();
        red += key == QLatin1StringView("high") ? 1 : 0;
        if (key == QLatin1StringView("notEnoughData")) {
            fail("a demo player has not enough data");
        }
    }
    if (red < 1 ||
        load->rowAt(0).value(QStringLiteral("statusKey")).toString() != QLatin1StringView("high")) {
        fail("the demo club has no red player at the top of the load table");
    }
    const qint64 spike = load->rowAt(0).value(QStringLiteral("playerId")).toLongLong();
    if (screen->view->property("selectedPlayer").toMap().value(QStringLiteral("playerId")) !=
        spike) {
        fail("the most urgent player is not selected");
    }
    auto* panel = screen->find(QStringLiteral("loadPlayerPanel"));
    auto* player = waitForPlayerLoad(panel, "the panel of the red player did not load");
    if (player == nullptr) {
        return;
    }
    // Keys, not words: the language is whatever an earlier scenario left.
    if (player->statusKey() != QLatin1StringView("high") || player->ratioPoints().isEmpty() ||
        !player->reasonKeys().contains(QStringLiteral("ratioHigh")) ||
        player->explanation().isEmpty()) {
        fail("the panel of the red player does not explain why");
    }
    auto* chart = findByObjectName(panel, QStringLiteral("loadChart"));
    expectFlag(chart, "visible", true, "the red player has no chart");
    expectFlag(findByObjectName(panel, QStringLiteral("ratioChartView")), "visible", true,
               "the red player has no ratio panel");

    // Hover over the last day with data: the tooltip finds the day by its index.
    if (auto* loadView = findByObjectName(panel, QStringLiteral("loadChartView"))) {
        renderFrame(screen->window);
        const QRectF area = loadView->property("plotArea").toRectF();
        const QPointF inside(area.x() + area.width() * 0.97, area.y() + area.height() / 2);
        const QPointF scene = loadView->mapToScene(inside);
        QMouseEvent move(QEvent::MouseMove, scene, scene, screen->window->mapToGlobal(scene),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(screen->window, &move);
        settle();
        if (chart->property("hoveredIndex").toInt() < 0) {
            fail("hovering the chart does not pick a day");
        }
    }

    auto* languages = engine.singletonInstance<LanguageManager*>(QStringLiteral("FootballDSS"),
                                                                 QStringLiteral("LanguageManager"));
    auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                            QStringLiteral("AppSettings"));
    const QString startLanguage = languages->language();
    const bool startDark = settings->darkTheme();
    for (const QString& language : {QStringLiteral("pl"), QStringLiteral("en")}) {
        for (const bool dark : {true, false}) {
            languages->setLanguage(language);
            settings->setDarkTheme(dark);
            settle();
            grabOrSave(screen->window, screenshotDirectory,
                       QStringLiteral("load-%1-%2.png")
                           .arg(language, dark ? QStringLiteral("dark") : QStringLiteral("light")));
        }
    }
    languages->setLanguage(startLanguage);
    settings->setDarkTheme(startDark);
    settle();

    // The minimum window keeps the table and a readable chart side by side.
    const QSize originalSize = screen->window->size();
    screen->window->resize(1280, 720);
    settle();
    renderFrame(screen->window);
    auto* table = screen->find(QStringLiteral("loadTable"));
    if (table == nullptr || chart->height() < 150 || chart->width() < 300 ||
        table->mapToScene(QPointF(table->width(), 0)).x() > panel->mapToScene(QPointF(0, 0)).x()) {
        fail("the load screen does not fit the minimum window");
    }
    grabOrSave(screen->window, screenshotDirectory, QStringLiteral("load-minimum.png"));
    expectLoadStatusesFit(engine, *screen);
    screen->window->resize(originalSize);
    settle();

    exerciseLoadNavigation(engine, *screen);

    // The same panel on the "Load" tab of the player card.
    screen->navigation->activate(QStringLiteral("squad"),
                                 QVariantMap{{QStringLiteral("playerId"), spike}});
    settle();
    auto* card = findQmlItem(screen->window->contentItem(), "PlayerCardView");
    auto* tabs = card != nullptr ? findQmlItem(card, "AppTabs") : nullptr;
    if (tabs == nullptr) {
        fail("the player card is not there");
        return;
    }
    tabs->setProperty("currentIndex", 1);
    settle();
    auto* cardPanel = card->property("loadPanel").value<QQuickItem*>();
    if (auto* cardLoad = waitForPlayerLoad(cardPanel, "the load tab of the card did not load")) {
        if (cardLoad->playerId() != spike || cardLoad->statusKey() != QLatin1StringView("high")) {
            fail("the load tab of the card does not show the red player");
        }
    }
    grabOrSave(screen->window, screenshotDirectory, QStringLiteral("card-load.png"));
}

// --- Dashboard and capabilities (P15) ---

void collectByObjectName(QQuickItem* item, const QString& name, QList<QQuickItem*>& found) {
    if (item->objectName() == name) {
        found.append(item);
    }
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        collectByObjectName(child, name, found);
    }
}

// The visible items of that name.
int countVisible(QQuickItem* root, const QString& name) {
    QList<QQuickItem*> found;
    collectByObjectName(root, name, found);
    return static_cast<int>(std::ranges::count_if(found, &QQuickItem::isVisible));
}

struct DashboardScreen {
    QQuickWindow* window = nullptr;
    NavigationModel* navigation = nullptr;
    fdss::viewmodels::DashboardViewModel* dashboard = nullptr;
    fdss::viewmodels::CapabilityModel* capabilities = nullptr;
    QQuickItem* view = nullptr;

    [[nodiscard]] QQuickItem* find(const QString& name) const {
        return findByObjectName(view, name);
    }
};

std::optional<DashboardScreen> openDashboard(QQmlApplicationEngine& engine) {
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        fail("Main.qml did not load");
        return std::nullopt;
    }
    settle();
    DashboardScreen screen;
    screen.window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    screen.navigation = screen.window->property("navigation").value<NavigationModel*>();
    screen.dashboard =
        screen.window->property("dashboard").value<fdss::viewmodels::DashboardViewModel*>();
    screen.capabilities =
        screen.window->property("capabilities").value<fdss::viewmodels::CapabilityModel*>();
    screen.view = findQmlItem(screen.window->contentItem(), "DashboardView");
    if (screen.navigation == nullptr || screen.dashboard == nullptr ||
        screen.capabilities == nullptr || screen.view == nullptr) {
        fail("the dashboard or its models are not loaded");
        return std::nullopt;
    }
    if (screen.navigation->currentKey() != QLatin1StringView("dashboard")) {
        fail("the shell does not start on the dashboard");
    }
    auto* dashboard = screen.dashboard;
    if (!waitUntil([dashboard] { return !dashboard->busy(); }, "the dashboard did not finish")) {
        return std::nullopt;
    }
    settle();
    return screen;
}

// The cards fit the page at the minimum window, side by side without overlap, and every row of a
// card is a hit target of at least Theme.hitMin.
void expectDashboardFits(const DashboardScreen& screen, const char* when) {
    QQuickItem* page = screen.find(QStringLiteral("dashboardPage"));
    if (page == nullptr) {
        fail("the dashboard has no page");
        return;
    }
    QList<QRectF> cards;
    for (const char* name : {"nextMatchCard", "unavailableCard", "loadSignalsCard", "formCard",
                             "todayCard", "featuresCard"}) {
        QQuickItem* card = screen.find(QString::fromLatin1(name));
        if (card == nullptr || !card->isVisible()) {
            qWarning("qml_smoke: %s: card %s missing", when, name);
            fail("a dashboard card is missing");
            continue;
        }
        const QRectF rect = card->mapRectToItem(page, card->boundingRect());
        if (rect.left() < 0 || rect.right() > page->width() + 0.5) {
            qWarning("qml_smoke: %s: card %s spans %.0f..%.0f, the page is %.0f wide", when, name,
                     rect.left(), rect.right(), page->width());
            fail("a dashboard card does not fit the page");
        }
        for (const QRectF& other : std::as_const(cards)) {
            if (rect.adjusted(1, 1, -1, -1).intersects(other)) {
                qWarning("qml_smoke: %s: card %s overlaps another card", when, name);
                fail("dashboard cards overlap");
            }
        }
        cards.append(rect);
    }
    for (const char* name : {"unavailableRow", "loadSignalRow", "formRow", "todaySessionRow"}) {
        QList<QQuickItem*> rows;
        collectByObjectName(screen.view, QString::fromLatin1(name), rows);
        for (QQuickItem* row : std::as_const(rows)) {
            if (row->isVisible() && row->height() < 32) {
                qWarning("qml_smoke: %s: a %s is %.0f px high", when, name, row->height());
                fail("a dashboard row is below the minimum hit target");
            }
        }
    }
}

void exerciseDashboardShortBaseline(const DashboardScreen& screen) {
    auto& registry = ServiceRegistry::instance();
    for (int day = 0; day < 14; ++day) {
        fdss::core::TrainingSessionDraft session;
        session.date = fdss::core::Date{std::chrono::sys_days{kSheetDay} - std::chrono::days{day}};
        const auto added = registry.calendar().addSession(smokeTeam(), session);
        if (!added) {
            fail("cannot add a session for the dashboard baseline");
            return;
        }
        auto sheet = registry.training().sheet(*added);
        if (!sheet || sheet->rows.empty()) {
            fail("cannot read the sheet for the dashboard baseline");
            return;
        }
        std::vector<fdss::core::AttendanceDraft> attendance;
        for (const auto& row : sheet->rows) {
            attendance.push_back(row.draft);
        }
        attendance.front().rpe = 6;
        const auto saved = registry.training().saveAttendance(*added, attendance);
        if (!saved || !saved->empty()) {
            fail("cannot save RPE for the dashboard baseline");
            return;
        }
    }
    screen.capabilities->reload();
    screen.dashboard->reload();
    if (!waitUntil([&screen] { return !screen.dashboard->busy(); },
                   "the dashboard baseline did not finish")) {
        return;
    }
    settle();
    auto* card = screen.find(QStringLiteral("loadSignalsCard"));
    const QString expected =
        screen.dashboard->loadSignalsEmpty().value(QStringLiteral("text")).toString();
    if (screen.capabilities->locks().contains(QStringLiteral("load")) || card == nullptr ||
        card->property("cardState").toString() != QLatin1StringView("empty") ||
        card->property("emptyText").toString() != expected ||
        !expected.startsWith(
            fdss::viewmodels::DashboardViewModel::tr("Not enough data. %1").arg(""))) {
        fail("fourteen RPE days show no signals instead of the missing baseline hint");
    }
}

// P19 decision 2: the Coach changes the season from the keyboard; a season that is not two
// consecutive years stays in the dialog with its error.
void exerciseTeamCard(QQmlApplicationEngine& engine, const DashboardScreen& screen) {
    auto* appState = engine.singletonInstance<AppState*>(QStringLiteral("FootballDSS"),
                                                         QStringLiteral("AppState"));
    screen.navigation->activate(QStringLiteral("settings"));
    settle();
    QQuickItem* root = screen.window->contentItem();
    auto* edit = findByObjectName(root, QStringLiteral("editTeamButton"));
    auto* settingsView = findQmlItem(root, "SettingsView");
    auto* dialog =
        settingsView != nullptr ? settingsView->property("teamDialog").value<QObject*>() : nullptr;
    if (edit == nullptr || dialog == nullptr || !edit->isVisible()) {
        fail("the Team card has no Edit button for the Coach");
        return;
    }
    const QString teamName = appState->teamName();
    const auto typeSeason = [&](const QString& season) {
        clickItem(screen.window, edit);
        settle();
        auto* seasonInput = findByObjectName(root, QStringLiteral("teamSeasonInput"));
        if (seasonInput == nullptr) {
            return false;
        }
        pressKey(screen.window, Qt::Key_Tab);
        seasonInput->setProperty("text", QString{});
        typeText(screen.window, season);
        pressKey(screen.window, Qt::Key_Return);
        settle();
        return true;
    };
    if (!typeSeason(QStringLiteral("2027/29"))) {
        fail("the Team dialog has no season field");
        return;
    }
    expectFlag(dialog, "visible", true, "a season of two years apart closed the Team dialog");
    if (findByObjectName(root, QStringLiteral("teamSeasonInput"))
            ->property("error")
            .toString()
            .isEmpty()) {
        fail("a season of two years apart shows no error");
    }
    QMetaObject::invokeMethod(dialog, "close");
    settle();
    if (!typeSeason(QStringLiteral("2027/28"))) {
        return;
    }
    expectFlag(dialog, "visible", false, "Enter did not save the team");
    const auto* season = findByObjectName(root, QStringLiteral("teamSeasonValue"));
    if (season == nullptr || season->property("text").toString() != QLatin1StringView("2027/28") ||
        appState->teamName() != teamName) {
        fail("the Team card does not show the saved season");
    }
}

void exerciseDashboardSettingsForEveryRole(QQmlApplicationEngine& engine,
                                           const DashboardScreen& screen) {
    const auto coach = ServiceRegistry::instance().session().user()->id;
    std::vector<fdss::core::StaffUserId> users{coach};
    for (const auto role :
         {fdss::core::Role::Assistant, fdss::core::Role::Physio, fdss::core::Role::Analyst}) {
        const auto user = addSmokeStaff(fdss::core::toDbText(role).data(), role);
        if (!user) {
            return;
        }
        users.push_back(*user);
    }
    auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                            QStringLiteral("AppSettings"));
    settings->setShowAdvancedModules(false);
    for (const auto user : users) {
        if (!logInAgain(engine, user)) {
            return;
        }
        for (const bool show : {true, false}) {
            screen.navigation->activate(QStringLiteral("settings"));
            settle();
            if (countVisible(screen.window->contentItem(), QStringLiteral("teamCard")) !=
                (user == coach ? 1 : 0)) {
                fail("the Team card in Settings is not shown to the Coach alone");
            }
            auto* toggle = findByObjectName(screen.window->contentItem(),
                                            QStringLiteral("showAdvancedModules"));
            if (toggle == nullptr || !toggle->isVisible() || !toggle->isEnabled()) {
                fail("a role cannot use the advanced modules toggle in Settings");
                return;
            }
            clickItem(screen.window, toggle);
            if (settings->showAdvancedModules() != show ||
                screen.capabilities->rowCount() != (show ? 6 : 5)) {
                fail("the Settings toggle does not change the capability list for every role");
            }
            screen.navigation->activate(QStringLiteral("dashboard"));
            settle();
            if (countVisible(screen.window->contentItem(), QStringLiteral("feature_eventData")) !=
                (show ? 1 : 0)) {
                fail("Event data does not follow the Settings toggle on the dashboard");
            }
        }
    }
    if (logInAgain(engine, coach)) {
        exerciseTeamCard(engine, screen);
    }
}

// Empty club: the dashboard and the locked Pre-match explain what to enter; after eleven players
// Pre-match unlocks (packet P15, "Done when").
void exerciseDashboardEmptyClub(QQmlApplicationEngine& engine, const QString& screenshotDirectory) {
    const auto screen = openDashboard(engine);
    if (!screen) {
        return;
    }
    for (const auto& [card, expected] :
         {std::pair{"nextMatchCard", "empty"}, std::pair{"unavailableCard", "empty"},
          std::pair{"loadSignalsCard", "locked"}, std::pair{"formCard", "locked"},
          std::pair{"todayCard", "empty"}}) {
        QQuickItem* item = screen->find(QString::fromLatin1(card));
        if (item == nullptr ||
            item->property("cardState").toString() != QLatin1StringView(expected)) {
            qWarning("qml_smoke: empty club: %s is not %s", card, expected);
            fail("a dashboard card of an empty club is in the wrong state");
        }
    }
    if (countVisible(screen->view, QStringLiteral("emptyAction")) < 3) {
        fail("the Coach gets no button in the empty cards of the dashboard");
    }
    if (screen->window->property("shell").value<QQuickItem*>() == nullptr ||
        countVisible(screen->window->contentItem(), QStringLiteral("nextMatchChip")) != 0) {
        fail("the top bar shows a next match for an empty club");
    }
    for (const bool dark : {true, false}) {
        auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                                QStringLiteral("AppSettings"));
        settings->setDarkTheme(dark);
        settle();
        if (resizeAndWait(screen->window, QSize(1280, 720))) {
            expectDashboardFits(*screen, dark ? "empty club, dark" : "empty club, light");
        }
        grabOrSave(screen->window, screenshotDirectory,
                   dark ? QStringLiteral("dashboard-empty-dark.png")
                        : QStringLiteral("dashboard-empty-light.png"));
    }

    // Pre-match is locked with the hint and a button to the squad.
    screen->navigation->activate(QStringLiteral("prematch"));
    settle();
    QQuickItem* locked =
        findByObjectName(screen->window->contentItem(), QStringLiteral("sectionLocked_prematch"));
    if (locked == nullptr || !locked->isVisible()) {
        fail("an empty club opens Pre-match without the locked state");
        return;
    }
    QQuickItem* hint = findByObjectName(locked, QStringLiteral("lockedHint"));
    if (hint == nullptr || !hint->property("text").toString().contains(QStringLiteral("11"))) {
        fail("the locked Pre-match does not count the players still needed");
    }
    QQuickItem* action = findByObjectName(locked, QStringLiteral("lockedAction"));
    if (action == nullptr || !action->isVisible()) {
        fail("the Coach gets no button to the squad from the locked Pre-match");
    }
    if (QQuickItem* word = findByObjectName(locked, QStringLiteral("lockedWord"));
        word == nullptr || !word->isVisible()) {
        fail("the locked state is shown without its word");
    }
    if (QQuickItem* view = findQmlItem(screen->window->contentItem(), "PreMatchView");
        view == nullptr || view->isVisible()) {
        fail("the locked Pre-match view is shown behind its locked state");
    }
    QMetaObject::invokeMethod(action, "clicked");
    settle();
    if (screen->navigation->currentKey() != QLatin1StringView("squad")) {
        fail("the button of the locked Pre-match does not open the squad");
    }

    // Eleven players: Pre-match unlocks on the next section change.
    for (int number = 1; number <= 11; ++number) {
        fdss::core::PlayerDraft draft;
        draft.firstName = "Gracz";
        draft.lastName = std::format("Numer{}", number);
        draft.shirtNumber = number;
        draft.primaryPosition = number == 1 ? fdss::core::Position::Gk : fdss::core::Position::Cm;
        if (!ServiceRegistry::instance().squad().addPlayer(smokeTeam(), draft)) {
            fail("cannot add a player for the dashboard");
            return;
        }
    }
    screen->navigation->activate(QStringLiteral("prematch"));
    settle();
    if (locked->isVisible() || screen->navigation->currentLocked()) {
        fail("Pre-match stays locked with eleven players");
    }
    screen->navigation->activate(QStringLiteral("dashboard"));
    settle();
    exerciseDashboardShortBaseline(*screen);
    exerciseDashboardSettingsForEveryRole(engine, *screen);
}

// Demo club: the next match, 2 injured + 1 suspended, the overloaded player and the form, in both
// themes at the minimum window (packet P15, "Done when").
void exerciseDashboardReadErrors(const DashboardScreen& screen,
                                 fdss::data::DatabaseManager& database) {
    // A live match keeps the overview independent of lineup attendance queries.
    QSqlQuery live(database.database());
    live.prepare(QStringLiteral("UPDATE match SET status = ? WHERE id = ?"));
    live.bindValue(0, QStringLiteral("live"));
    live.bindValue(1, screen.dashboard->nextMatchId());
    if (!live.exec()) {
        qWarning("%s",
                 database.sqlError(live.lastError(), "seed dashboard error test").detail.c_str());
        fail("cannot prepare the live match for the dashboard error test");
        return;
    }
    screen.navigation->activate(QStringLiteral("dashboard"));
    if (!waitUntil([&screen] { return !screen.dashboard->busy(); },
                   "the dashboard did not finish")) {
        return;
    }
    const auto reloadExpecting = [&screen](const char* message) {
        g_expectedCritical = message;
        g_expectedCriticalCount = 0;
        screen.dashboard->reload();
        g_expectedCritical = nullptr;
        if (g_expectedCriticalCount != 1) {
            fail("the dashboard did not report its failed read");
        }
        settle();
    };
    const auto expectErrorState = [](QQuickItem* item, const char* title) {
        if (item == nullptr || !item->isVisible() ||
            item->property("title").toString() !=
                QCoreApplication::translate("DashboardView", title) ||
            item->property("iconName").toString() != QLatin1StringView("danger") ||
            item->property("description").toString().isEmpty() ||
            item->property("actionText").toString() !=
                QCoreApplication::translate("DashboardView", "Retry")) {
            fail("a dashboard error lacks its icon, error title, description or Retry");
        }
    };
    auto* page = screen.find(QStringLiteral("dashboardPage"));
    auto* readError = screen.find(QStringLiteral("dashboardReadError"));
    auto* loadError = screen.find(QStringLiteral("loadSignalsReadError"));
    if (page == nullptr || readError == nullptr || loadError == nullptr) {
        fail("the dashboard error states are missing");
        return;
    }
    if (!database.execSql(QStringLiteral("ALTER TABLE attendance RENAME TO attendance_unavailable"),
                          "hide attendance for dashboard test")) {
        fail("cannot fail the dashboard load read");
        return;
    }
    reloadExpecting("no such table: attendance");
    expectErrorState(loadError, "Could not read load data");
    if (!page->isVisible() || readError->isVisible()) {
        fail("a failed load read hides the dashboard");
    }
    for (const char* name : {"nextMatchCard", "unavailableCard", "formCard", "todayCard"}) {
        auto* card = screen.find(QString::fromLatin1(name));
        if (card == nullptr || !card->isVisible() ||
            card->property("cardState").toString() != QLatin1StringView("content")) {
            fail("a failed load read hides or empties another dashboard card");
        }
    }
    const auto restoreAndRetry = [&database, &screen](const QString& sql, QQuickItem* error) {
        auto* retry = findQmlItem(error, "PrimaryButton");
        if (!database.execSql(sql, "restore dashboard test data") || retry == nullptr ||
            !retry->isVisible() || !QMetaObject::invokeMethod(retry, "clicked")) {
            fail("cannot retry the failed dashboard read");
            return;
        }
        waitUntil([&screen] { return !screen.dashboard->busy(); },
                  "the dashboard retry did not finish");
        settle();
        if (error->isVisible() || !screen.dashboard->ready() ||
            screen.dashboard->loadSignals().isEmpty()) {
            fail("Retry does not restore the dashboard data");
        }
    };
    restoreAndRetry(QStringLiteral("ALTER TABLE attendance_unavailable RENAME TO attendance"),
                    loadError);
    if (!database.execSql(
            QStringLiteral("ALTER TABLE availability RENAME TO availability_unavailable"),
            "hide availability for dashboard test")) {
        fail("cannot fail the dashboard overview read");
        return;
    }
    reloadExpecting("no such table: availability");
    expectErrorState(readError, "Could not load the dashboard");
    if (page->isVisible()) {
        fail("a failed overview read still shows the dashboard content");
    }
    restoreAndRetry(QStringLiteral("ALTER TABLE availability_unavailable RENAME TO availability"),
                    readError);
}

void exerciseDashboardDemo(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database,
                           const QString& screenshotDirectory) {
    if (!ServiceRegistry::instance().demoData().load(smokeTeam())) {
        fail("cannot load the demo club for the dashboard");
        return;
    }
    const auto screen = openDashboard(engine);
    if (!screen) {
        return;
    }
    if (!screen->capabilities->locks().isEmpty()) {
        fail("the demo club has a locked capability");
    }
    if (QQuickItem* card = screen->find(QStringLiteral("nextMatchCard"));
        card == nullptr || card->property("cardState").toString() != QLatin1StringView("content")) {
        fail("the demo club shows no next match");
    }
    if (countVisible(screen->window->contentItem(), QStringLiteral("nextMatchChip")) != 1) {
        fail("the top bar shows no days to the next match for the demo club");
    }
    if (countVisible(screen->view, QStringLiteral("unavailableRow")) != 3) {
        fail("the demo club does not show 2 injured and 1 suspended player");
    }
    if (countVisible(screen->view, QStringLiteral("loadSignalRow")) < 1) {
        fail("the demo club shows no load signal");
    }
    if (countVisible(screen->view, QStringLiteral("formRow")) != 5) {
        fail("the demo club does not show the last five matches");
    }
    for (const bool dark : {true, false}) {
        auto* settings = engine.singletonInstance<AppSettings*>(QStringLiteral("FootballDSS"),
                                                                QStringLiteral("AppSettings"));
        settings->setDarkTheme(dark);
        settle();
        if (resizeAndWait(screen->window, QSize(1280, 720))) {
            expectDashboardFits(*screen, dark ? "demo club, dark" : "demo club, light");
        }
        grabOrSave(screen->window, screenshotDirectory,
                   dark ? QStringLiteral("dashboard-demo-dark.png")
                        : QStringLiteral("dashboard-demo-light.png"));
    }
    // The chip opens Pre-match for the next match.
    QQuickItem* chip =
        findByObjectName(screen->window->contentItem(), QStringLiteral("nextMatchChip"));
    if (chip != nullptr) {
        QMetaObject::invokeMethod(chip, "clicked");
        settle();
        if (screen->navigation->currentKey() != QLatin1StringView("prematch") ||
            screen->navigation->currentArgs().value(QStringLiteral("matchId")).toLongLong() !=
                screen->dashboard->nextMatchId()) {
            fail("the chip of the top bar does not open Pre-match for the next match");
        }
    }
    exerciseDashboardReadErrors(*screen, database);
}

// --- Import (P16) ---

QString importFixture(const char* name) {
    return QStringLiteral(FDSS_IMPORT_FIXTURES_DIR "/%1").arg(QLatin1StringView(name));
}

int activePlayers(fdss::data::DatabaseManager& database) {
    QSqlQuery query(database.database());
    query.exec(QStringLiteral("SELECT count(*) FROM player WHERE archived_at IS NULL"));
    return query.next() ? query.value(0).toInt() : -1;
}

// A button of a step: in the window at its size, at least the minimum hit target high, enabled
// as expected.
void expectReachable(QQuickWindow* window, QQuickItem* item, bool enabled, const char* reason) {
    if (item == nullptr) {
        fail(reason);
        return;
    }
    renderFrame(window);
    const QRectF rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    const QRectF windowRect(0, 0, window->width(), window->height());
    constexpr qreal kMinHit = 32;
    if (!item->isVisible() || !windowRect.contains(rect) || rect.height() < kMinHit ||
        item->isEnabled() != enabled) {
        fail(reason);
    }
}

// A click where the item is on screen: a step just shown is laid out only when a frame is
// rendered (renderFrame()), so a click without one may land beside the button.
void clickPolished(QQuickWindow* window, QQuickItem* item) {
    if (item == nullptr) {
        fail("the item to click is not there");
        return;
    }
    renderFrame(window);
    clickItem(window, item);
}

// The whole wizard at the minimum window with the Polish Excel file of the packet (20 players),
// the undo, and a file without positions that asks for the default one. The file dialog is not
// driven: the file goes in through the viewmodel, everything else through the buttons.
void exerciseImport(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database,
                    const QString& screenshotDirectory) {
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    settle();
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    constexpr int kMinimumWidth = 1280;
    constexpr int kMinimumHeight = 720;
    const QSize originalSize = window->size();
    window->resize(kMinimumWidth, kMinimumHeight);
    navigation->activate(QStringLiteral("import"));
    settle();
    auto* view = findQmlItem(window->contentItem(), "ImportView");
    auto* wizard =
        view != nullptr ? view->property("viewModel").value<ImportWizardViewModel*>() : nullptr;
    if (wizard == nullptr) {
        fail("the import view or its viewmodel is not there");
        return;
    }
    const auto find = [view](const char* name) {
        return findByObjectName(view, QString::fromLatin1(name));
    };
    const auto atStep = [wizard](const char* step) {
        return !wizard->busy() && wizard->step() == QLatin1StringView(step);
    };

    clickPolished(window, find("importType_squad"));
    if (!atStep("file")) {
        fail("the squad type did not lead to the file step");
        return;
    }
    expectReachable(window, find("chooseFileButton"), true, "the file button is not usable");
    wizard->openPath(importFixture("polish-excel-cp1250.csv"));
    if (!waitUntil([&] { return atStep("preview"); }, "the Polish Excel file did not open")) {
        return;
    }
    settle();
    if (find("previewTable")->property("count").toInt() != 20) {
        fail("the preview does not show 20 rows");
    }
    expectReachable(window, find("toMappingButton"), true, "Next on the preview is not usable");
    clickPolished(window, find("toMappingButton"));
    if (!atStep("mapping") || find("mappingTable")->property("count").toInt() != 4) {
        fail("the mapping does not show the four columns");
        return;
    }
    if (find("mappingChoice0")->property("value").toString() != QLatin1StringView("last_name")) {
        fail("Nazwisko was not recognised as the last name");
    }
    expectReachable(window, find("validateButton"), true, "Check the rows is not usable");
    clickPolished(window, find("validateButton"));
    if (!waitUntil([&] { return atStep("report"); }, "the validation did not finish")) {
        return;
    }
    settle();
    // The rows of the table itself: the list inside it.
    auto* reportRows = findByObjectName(find("reportTable"), QStringLiteral("tableRows"));
    if (reportRows == nullptr || reportRows->property("count").toInt() != 20 ||
        wizard->okCount() != 20) {
        fail("the report does not have 20 rows without problems");
    }
    expectReachable(window, find("commitButton"), true, "Import is not usable on the report");
    grabOrSave(window, screenshotDirectory, QStringLiteral("import-report.png"));
    clickPolished(window, find("commitButton"));
    if (!atStep("summary") || activePlayers(database) != 20) {
        fail("the import did not write the 20 players");
        return;
    }
    expectReachable(window, find("undoImportButton"), true, "Undo import is not usable");
    clickPolished(window, find("undoImportButton"));
    auto* dialog = view->property("undoImportDialog").value<QObject*>();
    expectFlag(dialog, "visible", true, "Undo import did not ask first");
    QMetaObject::invokeMethod(dialog, "triggerPrimary");
    settle();
    if (activePlayers(database) != 0 || !wizard->undone()) {
        fail("undo import did not remove the players");
    }
    expectFlag(find("undoImportButton"), "visible", false, "Undo import is offered twice");

    // Without a position column: the default position starts as CM and can be changed.
    clickPolished(window, find("importAgainButton"));
    clickPolished(window, find("importType_squad"));
    wizard->openPath(importFixture("no-position.csv"));
    if (!waitUntil([&] { return atStep("preview"); }, "the file without positions did not open")) {
        return;
    }
    clickPolished(window, find("toMappingButton"));
    settle();
    expectReachable(window, find("defaultPositionChoice"), true,
                    "the default position cannot be chosen");
    if (find("defaultPositionChoice")->property("value").toString() != QLatin1StringView("cm")) {
        fail("the default position does not start as CM");
    }
    expectReachable(window, find("validateButton"), true,
                    "the rows cannot be checked with the default position");
    grabOrSave(window, screenshotDirectory, QStringLiteral("import-mapping.png"));
    wizard->reset();
    window->resize(originalSize);
    settle();
}

// --- Import of training and matches (P18) ---

int countRows(fdss::data::DatabaseManager& database, const char* sql) {
    QSqlQuery query(database.database());
    query.exec(QString::fromLatin1(sql));
    return query.next() ? query.value(0).toInt() : -1;
}

// Training and matches at the minimum window: the Polish Excel files of the packet, the options
// of the report, the commit, the summary with the notice about goals and cards, the undo without
// a question and, after a note was added, the undo that asks first; "Create metric…" opens the
// metric dialog.
void exerciseActivityImport(QQmlApplicationEngine& engine, fdss::data::DatabaseManager& database,
                            const QString& screenshotDirectory) {
    auto& registry = ServiceRegistry::instance();
    for (const auto& [first, last, number] :
         {std::tuple{"Jan", "Kowalski", 9}, std::tuple{"Piotr", "Nowak", 4},
          std::tuple{"Adam", "Nowak", 5}, std::tuple{"Łukasz", "Wiśniewski", 7}}) {
        fdss::core::PlayerDraft draft;
        draft.firstName = first;
        draft.lastName = last;
        draft.shirtNumber = number;
        if (!registry.squad().addPlayer(smokeTeam(), draft)) {
            fail("the import test cannot add a player");
            return;
        }
    }
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    settle();
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    constexpr int kMinimumWidth = 1280;
    constexpr int kMinimumHeight = 720;
    const QSize originalSize = window->size();
    window->resize(kMinimumWidth, kMinimumHeight);
    navigation->activate(QStringLiteral("import"));
    settle();
    auto* view = findQmlItem(window->contentItem(), "ImportView");
    auto* wizard =
        view != nullptr ? view->property("viewModel").value<ImportWizardViewModel*>() : nullptr;
    if (wizard == nullptr) {
        fail("the import view or its viewmodel is not there");
        return;
    }
    const auto find = [view](const char* name) {
        return findByObjectName(view, QString::fromLatin1(name));
    };
    const auto atStep = [wizard](const char* step) {
        return !wizard->busy() && wizard->step() == QLatin1StringView(step);
    };
    expectReachable(window, find("save_training_xlsx"), true, "the training template is missing");
    expectReachable(window, find("save_matches_csv"), true, "the match template is missing");

    // Training: to the report.
    clickPolished(window, find("importType_training"));
    wizard->openPath(importFixture("training-cp1250.csv"));
    if (!waitUntil([&] { return atStep("preview"); }, "the training file did not open")) {
        return;
    }
    clickPolished(window, find("toMappingButton"));
    if (!atStep("mapping")) {
        fail("the training file did not reach the mapping");
        return;
    }
    // "Create metric…" on the metric column opens the P17 dialog, prefilled.
    wizard->setColumnField(7, QStringLiteral("create_metric"));
    settle();
    auto* metricDialog = view->property("createMetricDialog").value<QObject*>();
    expectFlag(metricDialog, "visible", true, "Create metric did not open the metric dialog");
    QMetaObject::invokeMethod(metricDialog, "close");
    settle();
    expectReachable(window, find("validateButton"), true, "Check the rows is not usable");
    clickPolished(window, find("validateButton"));
    if (!waitUntil([&] { return atStep("report"); }, "the training validation did not finish")) {
        return;
    }
    settle();
    expectReachable(window, find("unknownPlayersChoice"), true,
                    "the choice for unknown players is not usable");
    expectReachable(window, find("commitButton"), true, "Import is not usable on the report");
    grabOrSave(window, screenshotDirectory, QStringLiteral("import-training-report.png"));
    clickPolished(window, find("commitButton"));
    if (!atStep("summary") || countRows(database, "SELECT count(*) FROM training_session") != 12) {
        fail("the training import did not write the 12 sessions");
        return;
    }
    expectReachable(window, find("openLoadButton"), true, "Open Load is not usable");
    // Nothing was added after the import: the undo asks nothing.
    clickPolished(window, find("undoImportButton"));
    settle();
    if (countRows(database, "SELECT count(*) FROM training_session") != 0 || !wizard->undone()) {
        fail("undo of the training import did not remove the sessions");
    }

    // Matches: the notice about goals and cards, then the undo after a note.
    clickPolished(window, find("importAgainButton"));
    clickPolished(window, find("importType_matches"));
    wizard->openPath(importFixture("matches-cp1250.csv"));
    if (!waitUntil([&] { return atStep("preview"); }, "the match file did not open")) {
        return;
    }
    clickPolished(window, find("toMappingButton"));
    clickPolished(window, find("validateButton"));
    if (!waitUntil([&] { return atStep("report"); }, "the match validation did not finish")) {
        return;
    }
    settle();
    expectFlag(find("notImportedLine"), "visible", true,
               "the report does not say that goals and cards are not imported");
    clickPolished(window, find("commitButton"));
    if (!atStep("summary") ||
        countRows(database, "SELECT count(*) FROM match WHERE status = 'finished'") != 3) {
        fail("the match import did not write the 3 matches");
        return;
    }
    grabOrSave(window, screenshotDirectory, QStringLiteral("import-matches-summary.png"));
    QSqlQuery note(database.database());
    note.exec(QStringLiteral("INSERT INTO match_note (match_id, minute, text) "
                             "SELECT id, 10, 'Notatka' FROM match LIMIT 1"));
    clickPolished(window, find("undoImportButton"));
    settle();
    auto* question = view->property("activityUndoImportDialog").value<QObject*>();
    expectFlag(question, "visible", true, "the undo did not name the note added later");
    QMetaObject::invokeMethod(question, "triggerPrimary");
    settle();
    if (countRows(database, "SELECT count(*) FROM match") != 0 ||
        countRows(database, "SELECT count(*) FROM match_note") != 0) {
        fail("undo of the match import did not remove the matches and the note");
    }
    wizard->reset();
    window->resize(originalSize);
    settle();
}

// --- Custom metrics (P17) ---

// The item lies inside the other one, both laid out on screen.
bool liesInside(QQuickItem* inner, QQuickItem* outer) {
    if (inner == nullptr || outer == nullptr) {
        return false;
    }
    const QRectF innerRect = inner->mapRectToScene(QRectF(0, 0, inner->width(), inner->height()));
    const QRectF outerRect = outer->mapRectToScene(QRectF(0, 0, outer->width(), outer->height()));
    // Half a pixel: layouts place items at fractional positions.
    return outerRect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(innerRect);
}

// "Done when" of the packet, at the minimum window: define "Sprint 30 m [s], decimal, lower is
// better, training" in Settings, enter values for a session from the keyboard, and see the latest
// value, the 28-day average and the arrow on the player card. Then the leave guard and the
// read-only Physio.
void exerciseMetrics(QQmlApplicationEngine& engine, const QString& screenshotDirectory) {
    engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
    settle();
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    auto* navigation = window->property("navigation").value<NavigationModel*>();
    constexpr int kMinimumWidth = 1280;
    constexpr int kMinimumHeight = 720;
    constexpr qreal kMinHit = 32;
    const QSize originalSize = window->size();
    window->resize(kMinimumWidth, kMinimumHeight);

    // Players and two sessions, the last one today; a value of the week before for the arrow.
    auto& registry = ServiceRegistry::instance();
    const auto today = seedSheet(3);
    if (!today) {
        return;
    }
    fdss::core::TrainingSessionDraft earlier;
    earlier.date =
        fdss::core::Date{std::chrono::year{2026}, std::chrono::October, std::chrono::day{25}};
    const auto earlierSession = registry.calendar().addSession(smokeTeam(), earlier);
    if (!earlierSession) {
        fail("the metrics test cannot add the earlier session");
        return;
    }

    // Settings: the empty card offers the first metric.
    navigation->activate(QStringLiteral("settings"));
    settle();
    auto* settings = findQmlItem(window->contentItem(), "SettingsView");
    auto* card =
        settings != nullptr ? settings->property("metricsSection").value<QQuickItem*>() : nullptr;
    if (card == nullptr) {
        fail("the metrics card is not in the settings");
        return;
    }
    // The Coach has Staff and the metrics in tabs; the metrics are the second one.
    auto* tabs = findByObjectName(settings, QStringLiteral("settingsTabs"));
    if (tabs == nullptr || !tabs->isVisible()) {
        fail("the settings of the Coach have no tabs");
        return;
    }
    tabs->setProperty("currentIndex", 1);
    settle();
    auto* definitions = card->property("viewModel").value<MetricDefinitionsViewModel*>();
    auto* dialog = card->property("dialog").value<QObject*>();
    const auto inCard = [&](const char* name) {
        return findByObjectName(card, QString::fromLatin1(name));
    };
    expectReachable(window, inCard("addMetricButton"), true, "Add metric is not usable");
    if (!liesInside(inCard("addMetricButton"), card)) {
        fail("Add metric is outside the metrics card at the minimum window");
    }
    clickPolished(window, inCard("addMetricButton"));
    expectFlag(dialog, "visible", true, "Add metric did not open the dialog");
    // The dialog opens on the Polish name, from the keyboard.
    typeText(window, QStringLiteral("Sprint 30 m"));
    pressKey(window, Qt::Key_Tab);
    typeText(window, QStringLiteral("Sprint 30 m"));
    auto* unit = findByObjectName(window->contentItem(), QStringLiteral("metricUnitInput"));
    auto* higher = findByObjectName(window->contentItem(), QStringLiteral("metricHigherInput"));
    if (unit == nullptr || higher == nullptr) {
        fail("the metric dialog is not all there");
        return;
    }
    auto* minimum = findByObjectName(window->contentItem(), QStringLiteral("metricMinInput"));
    auto* maximum = findByObjectName(window->contentItem(), QStringLiteral("metricMaxInput"));
    if (minimum == nullptr || maximum == nullptr) {
        fail("the metric dialog has no range fields");
        return;
    }
    unit->setProperty("text", QStringLiteral("s"));
    minimum->setProperty("text", QStringLiteral("3"));
    maximum->setProperty("text", QStringLiteral("10,0"));
    higher->setProperty("checked", false);
    expectReachable(window, maximum, true, "the maximum of the dialog is not in the window");
    QMetaObject::invokeMethod(dialog, "triggerPrimary");
    settle();
    expectFlag(dialog, "visible", false, "the metric dialog did not save");
    if (definitions->definitions()->count() != 1) {
        fail("the metric was not added");
        return;
    }
    const QVariantMap defined = definitions->definitions()->rowAt(0);
    if (defined.value(QStringLiteral("scope")).toString() != QLatin1StringView("training") ||
        defined.value(QStringLiteral("valueType")).toString() != QLatin1StringView("decimal") ||
        defined.value(QStringLiteral("higherIsBetter")).toBool()) {
        fail("the metric was not saved as a decimal training metric, lower is better");
    }
    const fdss::core::MetricDefinitionId metric{
        defined.value(QStringLiteral("metricId")).toLongLong()};
    grabOrSave(window, screenshotDirectory, QStringLiteral("metrics-settings.png"));

    // The player of row 0 ran 4,50 a week ago.
    const auto sheet = registry.metrics().sheet(metric, smokeTeam(), *earlierSession);
    if (!sheet || sheet->rows.empty()) {
        fail("the metrics test cannot read the earlier sheet");
        return;
    }
    const fdss::core::PlayerId player = sheet->rows.front().player.id;
    const std::vector<fdss::core::MetricValueDraft> older{{player, 4.5}};
    if (!registry.metrics().saveValues(metric, smokeTeam(), *earlierSession, older)) {
        fail("the metrics test cannot save the earlier value");
        return;
    }

    // The Metrics section opens the session of today; the values go in from the keyboard.
    navigation->activate(QStringLiteral("metrics"));
    settle();
    auto* view = findQmlItem(window->contentItem(), "MetricsView");
    auto* entry =
        view != nullptr ? view->property("viewModel").value<MetricEntryViewModel*>() : nullptr;
    if (entry == nullptr) {
        fail("the metrics view or its viewmodel is not there");
        return;
    }
    const auto find = [view](const QString& name) { return findByObjectName(view, name); };
    if (entry->targetId() != today->value || entry->count() != 3) {
        fail("the metrics view did not open the session of today with three players");
        return;
    }
    auto* firstCell = find(QStringLiteral("metricValueCell_0"));
    auto* grid = find(QStringLiteral("metricValueGrid"));
    renderFrame(window);
    if (firstCell == nullptr || firstCell->height() < kMinHit || !liesInside(firstCell, grid)) {
        fail("the value cell is too small or outside the grid");
        return;
    }
    if (!liesInside(find(QStringLiteral("metricSaveButton")), view)) {
        fail("Save of the metrics is outside the view at the minimum window");
    }
    firstCell->forceActiveFocus(Qt::TabFocusReason);
    settle();
    typeText(window, QStringLiteral("4,30"));
    pressKey(window, Qt::Key_Return);
    if (!find(QStringLiteral("metricValueCell_1"))->hasActiveFocus()) {
        fail("Enter did not go to the next row");
    }
    typeText(window, QStringLiteral("99"));
    settle();
    if (!entry->index(1).data(MetricEntryViewModel::HasErrorRole).toBool() ||
        !find(QStringLiteral("metricRowError_1"))->isVisible()) {
        fail("a value out of range shows no error in its row");
    }
    expectWrapsInside(find(QStringLiteral("metricRowError_1")),
                      "the range error does not wrap inside its row");
    pressKey(window, Qt::Key_Backspace);
    pressKey(window, Qt::Key_Backspace);
    if (!entry->dirty() || !entry->canSave()) {
        fail("the typed value does not make the grid savable");
    }
    grabOrSave(window, screenshotDirectory, QStringLiteral("metrics-entry.png"));
    QKeyEvent savePress(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier);
    QKeyEvent saveRelease(QEvent::KeyRelease, Qt::Key_S, Qt::ControlModifier);
    QCoreApplication::sendEvent(window, &savePress);
    QCoreApplication::sendEvent(window, &saveRelease);
    settle();
    if (entry->dirty()) {
        fail("Ctrl+S did not save the metric values");
    }

    // The leave guard: a change asks before another section.
    find(QStringLiteral("metricValueCell_2"))->forceActiveFocus(Qt::TabFocusReason);
    settle();
    typeText(window, QStringLiteral("5"));
    navigation->activate(QStringLiteral("dashboard"));
    settle();
    auto* question = view->property("leaveDialog").value<QObject*>();
    if (navigation->currentKey() != QLatin1StringView("metrics") ||
        !question->property("visible").toBool()) {
        fail("leaving the metrics with changes did not ask first");
    }
    QMetaObject::invokeMethod(question, "dangerClicked");
    settle();
    if (navigation->currentKey() != QLatin1StringView("dashboard") || entry->dirty()) {
        fail("Discard changes did not leave the metrics");
    }

    // The player card: 4,30 s after 4,50 s is faster, so the arrow is down and green.
    navigation->activate(QStringLiteral("squad"),
                         QVariantMap{{QStringLiteral("playerId"), qint64{player.value}}});
    settle();
    auto* playerCard = findQmlItem(window->contentItem(), "PlayerCardView");
    auto* metrics = playerCard != nullptr
                        ? playerCard->property("metricsModel").value<PlayerMetricsViewModel*>()
                        : nullptr;
    if (metrics == nullptr || metrics->count() != 1) {
        fail("the player card does not show the metric");
    } else {
        const QModelIndex row = metrics->index(0);
        if (row.data(PlayerMetricsViewModel::TrendRole).toString() != QLatin1StringView("down") ||
            !row.data(PlayerMetricsViewModel::ImprovementRole).toBool()) {
            fail("the player card does not show a better sprint as down and better");
        }
        renderFrame(window);
        auto* trend = findByObjectName(playerCard, QStringLiteral("playerMetricTrend_0"));
        auto* trendWord =
            trend != nullptr ? findByObjectName(trend, QStringLiteral("trendText")) : nullptr;
        auto* metricsCard = findByObjectName(playerCard, QStringLiteral("playerMetricsCard"));
        if (trend == nullptr || !trend->isVisible() || trendWord == nullptr ||
            trendWord->property("text").toString().isEmpty()) {
            fail("the arrow of the metric has no word next to it");
        } else if (!liesInside(trend, metricsCard)) {
            fail("the arrow of the metric is outside its card at the minimum window");
        }
        expectWrapsInside(findByObjectName(playerCard, QStringLiteral("playerMetricWindow_0")),
                          "the 28-day line does not wrap inside the metrics card");
        grabOrSave(window, screenshotDirectory, QStringLiteral("metrics-player-card.png"));
    }

    // Settings -> Metrics -> Settings: the card reads the list again when it is shown, so clearing
    // every value in Metrics unlocks Delete (review of P17).
    navigation->activate(QStringLiteral("settings"));
    settle();
    auto* table = card->property("table").value<QQuickItem*>();
    table->setProperty("currentIndex", 0);
    settle();
    expectFlag(inCard("deleteMetricButton"), "enabled", false,
               "a metric with values can be deleted");
    navigation->activate(QStringLiteral("metrics"));
    settle();
    for (const qint64 session : {qint64{earlierSession->value}, qint64{today->value}}) {
        entry->openSession(session);
        for (int row = 0; row < entry->count(); ++row) {
            entry->clearValue(row);
        }
        if (entry->dirty() && !entry->save()) {
            fail("the values of the metric could not be cleared");
        }
    }
    navigation->activate(QStringLiteral("settings"));
    settle();
    if (card->property("selectionHasValues").toBool() || definitions->isLocked(metric.value)) {
        fail("the settings still count the cleared values");
    }
    expectFlag(inCard("deleteMetricButton"), "enabled", true,
               "Delete stays off after every value was cleared in Metrics");

    // The Physio reads the values and changes nothing.
    const fdss::core::StaffUserId coach = registry.session().user()->id;
    const auto physio = addSmokeStaff("Piotr Fizjo", fdss::core::Role::Physio);
    if (!physio || !logInAgain(engine, *physio)) {
        return;
    }
    navigation->activate(QStringLiteral("metrics"));
    settle();
    view = findQmlItem(window->contentItem(), "MetricsView");
    entry = view != nullptr ? view->property("viewModel").value<MetricEntryViewModel*>() : nullptr;
    if (entry == nullptr) {
        fail("the metrics view is not there for the Physio");
    } else {
        expectFlag(entry, "readOnly", true, "the Physio may enter metric values");
        expectFlag(findByObjectName(view, QStringLiteral("metricSaveButton")), "visible", false,
                   "the Physio sees Save of the metrics");
        expectFlag(findByObjectName(view, QStringLiteral("metricValueCell_0")), "enabled", false,
                   "the Physio can type a metric value");
    }
    navigation->activate(QStringLiteral("settings"));
    settle();
    settings = findQmlItem(window->contentItem(), "SettingsView");
    card =
        settings != nullptr ? settings->property("metricsSection").value<QQuickItem*>() : nullptr;
    if (card == nullptr) {
        fail("the Physio does not see the metrics card");
    } else {
        expectFlag(findByObjectName(card, QStringLiteral("addMetricButton")), "visible", false,
                   "the Physio sees Add metric");
    }
    logInAgain(engine, coach);
    window->resize(originalSize);
    settle();
}

int main(int argc, char* argv[]) {
    // As the app does; without it this run shows the CI failures of P12 under QV4_GC_TIMELIMIT=1.
    fdss::viewmodels::useNonIncrementalGarbageCollector();
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
        g_withoutFocus = arguments.contains(QStringLiteral("--without-focus"));
        if (const qsizetype flag = arguments.indexOf(QStringLiteral("--screenshots"));
            flag >= 0 && flag + 1 < arguments.size()) {
            screenshotDirectory = arguments.at(flag + 1);
        }

        // qml_smoke --only-load runs the load scenarios alone, in the default language of the
        // machine (P11 review: they must not depend on an earlier scenario).
        const bool onlyLoad = arguments.contains(QStringLiteral("--only-load"));
        // qml_smoke --only-prematch runs the pre-match scenario alone (P12).
        const bool onlyPreMatch = arguments.contains(QStringLiteral("--only-prematch"));
        // qml_smoke --only-dashboard runs the dashboard scenarios alone (P15).
        const bool onlyDashboard = arguments.contains(QStringLiteral("--only-dashboard"));
        // qml_smoke --only-import runs the import wizard alone (P16).
        const bool onlyImport = arguments.contains(QStringLiteral("--only-import"));
        // qml_smoke --only-metrics runs the custom metrics alone (P17).
        const bool onlyMetrics = arguments.contains(QStringLiteral("--only-metrics"));
        const bool onlyOne = onlyLoad || onlyPreMatch || onlyDashboard || onlyImport || onlyMetrics;

        std::optional<UiPreferences> leftBehind;
        for (const AppState::State state :
             {AppState::State::NeedsSetup, AppState::State::NeedsLogin, AppState::State::Ready}) {
            if (onlyOne) {
                break;
            }
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
                if (state == AppState::State::NeedsLogin) {
                    exerciseTileScrolling(engine);
                }
                if (state == AppState::State::Ready) {
                    exerciseSquad(engine, screenshotDirectory);
                    exerciseCalendar(engine, *database, screenshotDirectory);
                    exerciseDatabaseErrorView(engine, screenshotDirectory);
                    viewCount = instantiateEveryView(engine);
                    exerciseLoginOverSession(engine);
                    exerciseLogoutOnUnreadableDatabase(engine, *database);
                }
            }
            settle();
            ServiceRegistry::reset();
        }
        if (onlyOne) {
            // Nothing ran before.
        } else if (installRegistry(AppState::State::Ready) == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                exerciseTraining(engine, screenshotDirectory);
            }
            settle();
            ServiceRegistry::reset();
        }
        if (onlyLoad || onlyDashboard || onlyImport || onlyMetrics) {
            // Not this one.
        } else if (installRegistry(AppState::State::Ready) == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                exercisePreMatch(engine, screenshotDirectory);
            }
            settle();
            ServiceRegistry::reset();
        }
        if (onlyOne) {
            // Not this one.
        } else if (fdss::data::DatabaseManager* database = installRegistry(AppState::State::Ready);
                   database == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                exerciseMatchDay(engine, *database, screenshotDirectory);
            }
            settle();
            ServiceRegistry::reset();
        }
        if (onlyLoad || onlyPreMatch || onlyDashboard || onlyMetrics) {
            // Not this one.
        } else if (fdss::data::DatabaseManager* database = installRegistry(AppState::State::Ready);
                   database == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                exerciseImport(engine, *database, screenshotDirectory);
            }
            settle();
            ServiceRegistry::reset();
        }
        if (onlyLoad || onlyPreMatch || onlyDashboard || onlyMetrics) {
            // Not this one.
        } else if (fdss::data::DatabaseManager* database = installRegistry(AppState::State::Ready);
                   database == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                exerciseActivityImport(engine, *database, screenshotDirectory);
            }
            settle();
            ServiceRegistry::reset();
        }
        if (onlyLoad || onlyPreMatch || onlyDashboard || onlyImport) {
            // Not this one.
        } else if (installRegistry(AppState::State::Ready) == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                exerciseMetrics(engine, screenshotDirectory);
            }
            settle();
            ServiceRegistry::reset();
        }
        // A closed window must not end the application: the next scenario still has to run.
        QGuiApplication::setQuitOnLastWindowClosed(false);
        for (const CloseCase closeCase : {CloseCase::Clean, CloseCase::StayThenDiscard,
                                          CloseCase::Save, CloseCase::FailedSaveThenDiscard}) {
            if (!onlyOne) {
                exerciseWindowClosing(closeCase);
            }
        }
        QGuiApplication::setQuitOnLastWindowClosed(true);
        if (onlyOne) {
            // Done.
        } else if (installRegistry(AppState::State::NeedsSetup) == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                engine.loadFromModule(QStringLiteral("FootballDSS"), QStringLiteral("Main"));
                settle();
                exerciseUserMarkup(engine);
            }
            settle();
            ServiceRegistry::reset();
        }
        if (onlyOne) {
            // Done.
        } else if (installRegistry(AppState::State::NeedsSetup) == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                exerciseDemoSetup(engine);
            }
            settle();
            ServiceRegistry::reset();
        }
        if (onlyOne) {
            // Done.
        } else if (auto* database = installRegistry(AppState::State::NeedsSetup);
                   database == nullptr) {
            fail("cannot install the service registry");
        } else {
            {
                QQmlApplicationEngine engine;
                exerciseFailedDemoSetup(engine, *database);
            }
            settle();
            ServiceRegistry::reset();
        }
        if (leftBehind) {
            verifyPreferencesSurviveRestart(*leftBehind);
        }
        for (const bool demo : {false, true}) {
            if (onlyLoad || onlyPreMatch || onlyImport || onlyMetrics) {
                break;
            }
            auto* database = installRegistry(AppState::State::Ready);
            if (database == nullptr) {
                fail("cannot install the service registry");
                continue;
            }
            {
                QQmlApplicationEngine engine;
                if (demo) {
                    exerciseDashboardDemo(engine, *database, screenshotDirectory);
                } else {
                    exerciseDashboardEmptyClub(engine, screenshotDirectory);
                }
            }
            settle();
            ServiceRegistry::reset();
        }
        // Last: the load scenarios change the language they start in.
        // In the full run they start in Polish, as an earlier scenario may leave it.
        const std::optional<QString> loadLanguage =
            onlyLoad ? std::nullopt : std::optional(QStringLiteral("pl"));
        for (const bool demo : {false, true}) {
            if (onlyPreMatch || onlyDashboard || onlyImport || onlyMetrics) {
                break;
            }
            if (installRegistry(AppState::State::Ready) == nullptr) {
                fail("cannot install the service registry");
                continue;
            }
            {
                QQmlApplicationEngine engine;
                if (demo) {
                    exerciseLoadDemo(engine, loadLanguage, screenshotDirectory);
                } else {
                    exerciseLoadNewClub(engine, loadLanguage, screenshotDirectory);
                }
            }
            settle();
            ServiceRegistry::reset();
        }
        QSettings().clear();

        if (viewCount == 0 && !onlyOne) {
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
