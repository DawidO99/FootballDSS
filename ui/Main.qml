pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

ApplicationWindow {
    id: window

    // Exposed for tests (qml_smoke drives navigation through it).
    readonly property NavigationModel navigation: navigationModel
    property alias sidebarCollapsed: sidebar.collapsed
    // P03 spike: Ctrl+Shift+G shows the Qt Graphs spike view instead of the shell. Not part of
    // NavigationModel; goes away together with SpikeChartsView.qml.
    property bool spikeChartsOpen: false
    // P06b: Ctrl+Shift+K toggles the hidden UI kit component gallery.
    property bool componentGalleryOpen: false
    readonly property Item componentGallery: galleryLoader.item as Item

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

    NavigationModel {
        id: navigationModel
    }

    RowLayout {
        anchors.fill: parent
        visible: !window.spikeChartsOpen && !window.componentGalleryOpen
        spacing: 0

        Sidebar {
            id: sidebar

            Layout.fillHeight: true
            navigation: navigationModel
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            TopBar {
                Layout.fillWidth: true
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: navigationModel.currentIndex

                Repeater {
                    model: navigationModel

                    delegate: Loader {
                        required property string key

                        sourceComponent: window.viewComponents[key] ?? null
                    }
                }
            }
        }
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
        id: dashboardView

        DashboardView {
            onNavigateRequested: key => navigationModel.activate(key)
        }
    }

    Component {
        id: squadView

        SquadView {
            onNavigateRequested: key => navigationModel.activate(key)
        }
    }

    Component {
        id: calendarView

        CalendarView {
            onNavigateRequested: key => navigationModel.activate(key)
        }
    }

    Component {
        id: trainingView

        TrainingView {
            onNavigateRequested: key => navigationModel.activate(key)
        }
    }

    Component {
        id: preMatchView

        PreMatchView {
            onNavigateRequested: key => navigationModel.activate(key)
        }
    }

    Component {
        id: matchDayView

        MatchDayView {
            onNavigateRequested: key => navigationModel.activate(key)
        }
    }

    Component {
        id: loadView

        LoadView {
            onNavigateRequested: key => navigationModel.activate(key)
        }
    }

    Component {
        id: importView

        ImportView {
            onNavigateRequested: key => navigationModel.activate(key)
        }
    }

    Component {
        id: settingsView

        SettingsView {
            onNavigateRequested: key => navigationModel.activate(key)
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
