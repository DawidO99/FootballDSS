import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shown instead of a feature that waits for data (SPEC section 2, packet P15): a lock, the word
// "Locked", what is missing and either a button to where it is entered or who can enter it.
// API:
//   title: string        what is locked
//   hint: string         what is missing, counted
//   actionText: string   the button; empty for a role that cannot enter the data
//   askText: string      who can enter it, shown without a button
//   compact: bool        inside a card: left-aligned, smaller
//   actionTriggered()
Item {
    id: root

    property string title
    property string hint
    property string actionText
    property string askText
    property bool compact: false

    signal actionTriggered

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Locked: %1").arg(root.title)
    Accessible.description: root.hint

    ColumnLayout {
        id: column

        anchors.centerIn: root.compact ? undefined : parent
        anchors.left: root.compact ? parent.left : undefined
        anchors.right: root.compact ? parent.right : undefined
        width: root.compact ? root.width : Math.min(root.width, Theme.emptyStateWidth)
        spacing: root.compact ? Theme.spacing.s : Theme.spacing.m

        RowLayout {
            Layout.alignment: root.compact ? Qt.AlignLeft : Qt.AlignHCenter
            spacing: Theme.spacing.s

            Icon {
                name: "lock"
                size: root.compact ? Theme.iconSize.s : Theme.iconSize.m
                color: Theme.warn
            }

            Label {
                objectName: "lockedWord"
                text: qsTr("Locked")
                color: Theme.warn
                font.pixelSize: Theme.fontSize.body
                font.weight: Font.DemiBold
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !root.compact && text.length > 0
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontSize.title
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Label {
            objectName: "lockedHint"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.hint
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.body
            horizontalAlignment: root.compact ? Text.AlignLeft : Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Label {
            objectName: "lockedAsk"
            Layout.fillWidth: true
            visible: root.actionText.length === 0 && text.length > 0
            text: root.askText
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.body
            font.italic: true
            horizontalAlignment: root.compact ? Text.AlignLeft : Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        PrimaryButton {
            objectName: "lockedAction"
            Layout.alignment: root.compact ? Qt.AlignLeft : Qt.AlignHCenter
            Layout.topMargin: root.compact ? 0 : Theme.spacing.s
            visible: text.length > 0
            text: root.actionText
            onClicked: root.actionTriggered()
        }
    }
}
