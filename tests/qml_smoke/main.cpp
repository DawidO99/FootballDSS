// Loads Main.qml in the three database states and every view offscreen. Fails on any warning,
// critical or fatal message, when the Polish translation is not applied, or when preferences do not
// survive a restart.

#include "../services/FakeClock.hpp"
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
#include "services/Session.hpp"
#include "services/SetupService.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/AppSettings.hpp"
#include "viewmodels/AppState.hpp"
#include "viewmodels/AvailabilityViewModel.hpp"
#include "viewmodels/CalendarViewModel.hpp"
#include "viewmodels/CalendarWeekModel.hpp"
#include "viewmodels/ChangePinViewModel.hpp"
#include "viewmodels/LanguageManager.hpp"
#include "viewmodels/MatchEditorViewModel.hpp"
#include "viewmodels/NavigationModel.hpp"
#include "viewmodels/PlayerCardViewModel.hpp"
#include "viewmodels/PlayerEditorViewModel.hpp"
#include "viewmodels/ProfilePickerViewModel.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SessionEditorViewModel.hpp"
#include "viewmodels/SetupWizardViewModel.hpp"
#include "viewmodels/SquadViewModel.hpp"
#include "viewmodels/StaffViewModel.hpp"
#include "viewmodels/StartupViewModel.hpp"

#include <QColor>
#include <QDate>
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
#include <QRectF>
#include <QSet>
#include <QSettings>
#include <QVariantMap>
#include <QtQml/QQmlExtensionPlugin>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
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
using fdss::viewmodels::AvailabilityViewModel;
using fdss::viewmodels::CalendarViewModel;
using fdss::viewmodels::ChangePinViewModel;
using fdss::viewmodels::LanguageManager;
using fdss::viewmodels::MatchEditorViewModel;
using fdss::viewmodels::NavigationModel;
using fdss::viewmodels::PlayerCardViewModel;
using fdss::viewmodels::PlayerEditorViewModel;
using fdss::viewmodels::ProfilePickerViewModel;
using fdss::viewmodels::ServiceRegistry;
using fdss::viewmodels::SessionEditorViewModel;
using fdss::viewmodels::SetupWizardViewModel;
using fdss::viewmodels::SquadViewModel;
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

    view->setProperty("mode", QStringLiteral("week"));
    capture("calendar-week");
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
    QMetaObject::invokeMethod(view, "openItem", Q_ARG(QVariant, QStringLiteral("match")),
                              Q_ARG(QVariant, double(currentMatchId)));
    capture("calendar-match-finished");
    expectFlag(matchDialog, "visible", true, "a finished match did not open");
    if (!matchDialog->property("primaryText").toString().isEmpty() ||
        !matchDialog->property("dangerText").toString().isEmpty()) {
        fail("a finished match can still be saved or deleted");
    }
    pressKey(window, Qt::Key_Escape);

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
        if (installRegistry(AppState::State::NeedsSetup) == nullptr) {
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
