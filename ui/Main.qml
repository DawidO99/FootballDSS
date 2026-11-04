pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

ApplicationWindow {
    id: window

    // Exposed for tests (qml_smoke drives navigation through it).
    readonly property NavigationModel navigation: navigationModel
    // Exposed for tests: null unless a user is logged in.
    readonly property Item shell: shellLoader.item as Item
    // Exposed for tests: null unless the state asks for them.
    readonly property Item setupWizard: setupLoader.item as Item
    readonly property Item profilePicker: pickerLoader.item as Item
    // Lives here, not on the Sidebar, so it survives a logout.
    property bool sidebarCollapsed: false
    // P03 spike: Ctrl+Shift+G shows the Qt Graphs spike view instead of the shell. Not part of
    // NavigationModel; goes away together with SpikeChartsView.qml.
    property bool spikeChartsOpen: false
    // P06b: Ctrl+Shift+K toggles the hidden UI kit component gallery.
    property bool componentGalleryOpen: false
    readonly property Item componentGallery: galleryLoader.item as Item
    // The view that can hold changes that are not saved; null without the shell.
    property TrainingView trainingPage: null
    // What runGuarded() waits to do; null: the guard of navigationModel has the target.
    property var pendingLeaveAction: null

    // Does the action, after asking about changes that are not saved when there are any.
    function runGuarded(action) {
        if (!navigationModel.leaveGuard || !window.trainingPage) {
            action();
            return;
        }
        window.pendingLeaveAction = action;
        window.trainingPage.askBeforeLeaving();
    }

    function finishLeaving() {
        const action = window.pendingLeaveAction;
        window.pendingLeaveAction = null;
        if (action) {
            navigationModel.leaveGuard = false;
            action();
        } else {
            navigationModel.resumeLeave();
        }
    }

    function cancelLeaving() {
        window.pendingLeaveAction = null;
        navigationModel.cancelLeave();
    }

    // Section key -> view. Keys come from NavigationModel.
    readonly property var viewComponents: ({
            "dashboard": dashboardView,
            "squad": squadView,
            "calendar": calendarView,
            "training": trainingView,
            "prematch": preMatchView,
            "matchday": matchDayView,
            "load": loadView,
            "import": importView,
            "settings": settingsView
        })

    width: 1600
    height: 900
    minimumWidth: 1280
    minimumHeight: 720
    visible: true
    title: qsTr("FootballDSS")
    color: Theme.bg
    font.pixelSize: Theme.fontSize.body

    // Standard controls (tooltips, scroll bars, text fields) pick their colors from the theme.
    palette.window: Theme.bg
    palette.windowText: Theme.text
    palette.base: Theme.surfaceAlt
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surfaceAlt
    palette.buttonText: Theme.text
    palette.brightText: Theme.accentText
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.placeholderText: Theme.textMuted
    palette.toolTipBase: Theme.surfaceAlt
    palette.toolTipText: Theme.text
    palette.light: Theme.surfaceAlt
    palette.midlight: Theme.surfaceAlt
    palette.mid: Theme.border
    palette.dark: Theme.accent
    palette.shadow: Theme.bg

    // Without changes this asks nothing and the window closes at once.
    onClosing: close => {
        if (navigationModel.leaveGuard && window.trainingPage) {
            close.accepted = false;
            window.runGuarded(() => window.close());
        }
    }

    NavigationModel {
        id: navigationModel

        onLeaveRequested: {
            if (window.trainingPage)
                window.trainingPage.askBeforeLeaving();
        }
    }

    Connections {
        target: AppState

        function onSessionSerialChanged() {
            // Whoever ended the session has answered for the changes; the views are going away.
            window.pendingLeaveAction = null;
            navigationModel.cancelLeave();
            navigationModel.leaveGuard = false;
            // An undo toast belongs to the last user, whose views are going away.
            Toasts.dismiss();
            // The next user starts on the dashboard: the section of the last one may be hidden.
            navigationModel.activate("dashboard");
            // A login over a running session leaves the state at Ready, so `active` does not change.
            // After a login from the picker the shell is already built for this session.
            if (shellLoader.item && shellLoader.builtForSession !== AppState.sessionSerial) {
                shellLoader.sourceComponent = null;
                shellLoader.sourceComponent = shellComponent;
            }
        }

        function onErrorOccurred(message: string) {
            Toasts.error(message);
        }
    }

    // The section views exist only for a logged-in user. Every login builds them anew, so their
    // AppState.can() bindings are computed for that user, and a logout leaves none of the data of
    // the last user in memory.
    Loader {
        id: shellLoader

        property int builtForSession: -1

        anchors.fill: parent
        active: AppState.state === AppState.Ready
        visible: !window.spikeChartsOpen && !window.componentGalleryOpen
        sourceComponent: shellComponent
        onLoaded: builtForSession = AppState.sessionSerial
    }

    Loader {
        id: setupLoader

        anchors.fill: parent
        active: AppState.state === AppState.NeedsSetup
        visible: !window.spikeChartsOpen && !window.componentGalleryOpen
        focus: active
        sourceComponent: setupWizardView
    }

    // Built anew for every logout, so nothing typed by the last user stays behind.
    Loader {
        id: pickerLoader

        anchors.fill: parent
        active: AppState.state === AppState.NeedsLogin
        visible: !window.spikeChartsOpen && !window.componentGalleryOpen
        focus: active
        sourceComponent: profilePickerView
    }

    Shortcut {
        sequence: "Ctrl+Shift+G"
        onActivated: {
            window.spikeChartsOpen = !window.spikeChartsOpen;
            if (window.spikeChartsOpen)
                window.componentGalleryOpen = false;
        }
    }

    Shortcut {
        sequence: "Ctrl+Shift+K"
        onActivated: {
            window.componentGalleryOpen = !window.componentGalleryOpen;
            if (window.componentGalleryOpen)
                window.spikeChartsOpen = false;
        }
    }

    Loader {
        anchors.fill: parent
        active: window.spikeChartsOpen
        sourceComponent: spikeChartsView
    }

    Loader {
        id: galleryLoader

        anchors.fill: parent
        active: window.componentGalleryOpen
        sourceComponent: componentGalleryView
    }

    ToastHost {
        parent: Overlay.overlay
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.xl
        width: Math.min(Theme.toastWidth, parent.width - 2 * Theme.spacing.xl)
        z: 1
    }

    Component {
        id: shellComponent

        RowLayout {
            spacing: 0

            Sidebar {
                id: sidebar

                Layout.fillHeight: true
                navigation: navigationModel
                onCollapsedChanged: window.sidebarCollapsed = collapsed
            }

            // Not a plain binding on the Sidebar: its toggle button writes the property and would
            // end that binding.
            Binding {
                target: sidebar
                property: "collapsed"
                value: window.sidebarCollapsed
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                TopBar {
                    Layout.fillWidth: true
                    onLogoutRequested: window.runGuarded(() => AppState.logout())
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: navigationModel.currentIndex

                    Repeater {
                        model: navigationModel

                        delegate: Loader {
                            id: sectionLoader

                            required property string key

                            sourceComponent: window.viewComponents[key] ?? null

                            // All sections stay loaded; only the current one gets the arguments,
                            // the others fall back to their default. A view reacts in
                            // onNavigationArgsChanged: the order against its visible change is not
                            // guaranteed.
                            Binding {
                                target: sectionLoader.item
                                property: "navigationArgs"
                                when: sectionLoader.status === Loader.Ready && navigationModel.currentKey === sectionLoader.key
                                value: navigationModel.currentArgs
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: setupWizardView

        SetupWizardView {}
    }

    Component {
        id: profilePickerView

        ProfilePickerView {}
    }

    Component {
        id: dashboardView

        DashboardView {
            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
        }
    }

    Component {
        id: squadView

        SquadView {
            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
        }
    }

    Component {
        id: calendarView

        CalendarView {
            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
        }
    }

    Component {
        id: trainingView

        TrainingView {
            id: trainingViewItem

            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
            onHasUnsavedChangesChanged: navigationModel.leaveGuard = hasUnsavedChanges
            onLeaveAccepted: window.finishLeaving()
            onLeaveCancelled: window.cancelLeaving()
            Component.onCompleted: window.trainingPage = trainingViewItem
        }
    }

    Component {
        id: preMatchView

        PreMatchView {
            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
        }
    }

    Component {
        id: matchDayView

        MatchDayView {
            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
        }
    }

    Component {
        id: loadView

        LoadView {
            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
        }
    }

    Component {
        id: importView

        ImportView {
            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
        }
    }

    Component {
        id: settingsView

        SettingsView {
            onNavigateRequested: (key, args) => navigationModel.activate(key, args)
        }
    }

    Component {
        id: spikeChartsView

        SpikeChartsView {}
    }

    Component {
        id: componentGalleryView

        ComponentGalleryView {}
    }
}
