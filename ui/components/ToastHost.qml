import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Renders the active toast from the Toasts singleton at the bottom of the window.
// Pauses the 10-second auto-dismiss timer while hovered.
// Status is never color-only: includes a StatusBadge (icon + word + color).
Item {
    id: root

    readonly property bool isError: Toasts.kind === StatusBadge.Danger

    visible: Toasts.active
    implicitWidth: Theme.toastWidth
    implicitHeight: banner.implicitHeight

    Connections {
        target: Toasts

        function onSerialChanged() {
            if (Toasts.active)
                dismissTimer.restart();
        }
    }

    Timer {
        id: dismissTimer

        interval: Theme.toastDuration
        running: Toasts.active && !hoverHandler.hovered
        onTriggered: Toasts.dismiss()
    }

    Rectangle {
        id: banner

        width: root.width
        implicitWidth: Theme.toastWidth
        implicitHeight: row.implicitHeight + 2 * Theme.spacing.m
        radius: Theme.radius.m
        color: Theme.surface
        border.width: Theme.focusRingWidth
        border.color: root.isError ? Theme.danger : Theme.info

        HoverHandler {
            id: hoverHandler
        }

        RowLayout {
            id: row

            anchors.fill: parent
            anchors.margins: Theme.spacing.m
            spacing: Theme.spacing.m

            StatusBadge {
                kind: Toasts.kind
                text: root.isError ? qsTr("Error") : qsTr("Info")
            }

            Label {
                Layout.fillWidth: true
                text: Toasts.text
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.AlertMessage
                Accessible.name: text
            }

            SecondaryButton {
                visible: Toasts.undoText.length > 0
                text: Toasts.undoText
                onClicked: Toasts.undo()
            }

            IconButton {
                iconName: "close"
                iconColor: Theme.textMuted
                text: qsTr("Dismiss notification")
                onClicked: Toasts.dismiss()
            }
        }
    }
}

