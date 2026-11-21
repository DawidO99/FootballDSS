import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A card of the dashboard (packet P15) with its own empty and locked state.
// API:
//   title: string, iconName: string
//   cardState: "content" | "empty" | "locked"
//   body (default): the content, shown in state "content"
//   emptyText, emptyActionText: the empty state; no button without emptyActionText
//   lockHint, lockActionText, lockAskText: the locked state (LockedState, compact)
//   emptyActionTriggered(), lockActionTriggered()
Card {
    id: root

    property string iconName
    property string cardState: "content"
    default property alias body: bodyColumn.data
    property string emptyText
    property string emptyActionText
    property string lockHint
    property string lockActionText
    property string lockAskText

    signal emptyActionTriggered
    signal lockActionTriggered

    Accessible.role: Accessible.Grouping
    Accessible.name: root.title

    content: [
        ColumnLayout {
            id: bodyColumn

            Layout.fillWidth: true
            visible: root.cardState === "content"
            spacing: Theme.spacing.s
        },
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.cardState === "empty"
            spacing: Theme.spacing.s

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.s

                Icon {
                    Layout.alignment: Qt.AlignTop
                    name: root.iconName.length > 0 ? root.iconName : "inbox"
                    size: Theme.iconSize.s
                    color: Theme.textMuted
                }

                Label {
                    objectName: "emptyText"
                    Layout.fillWidth: true
                    text: root.emptyText
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.WordWrap
                }
            }

            SecondaryButton {
                objectName: "emptyAction"
                visible: text.length > 0
                text: root.emptyActionText
                onClicked: root.emptyActionTriggered()
            }
        },
        LockedState {
            Layout.fillWidth: true
            visible: root.cardState === "locked"
            compact: true
            title: root.title
            hint: root.lockHint
            actionText: root.lockActionText
            askText: root.lockAskText
            onActionTriggered: root.lockActionTriggered()
        },
        Item {
            Layout.fillHeight: true
        }
    ]
}
