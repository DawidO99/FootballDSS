import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A clickable line of a dashboard card (P15): a name from user data, then its details.
// API:
//   text: string         the name, shown as plain text
//   details (default):   items below the name (badges, labels)
//   clicked()            Enter and Return click it too
AbstractButton {
    id: control

    default property alias details: detailsColumn.data

    implicitWidth: column.implicitWidth + leftPadding + rightPadding
    implicitHeight: Math.max(Theme.hitMin, column.implicitHeight + topPadding + bottomPadding)
    padding: Theme.spacing.s
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: text
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: Theme.radius.m
        color: control.hovered ? Theme.rowHover : "transparent"
        border.width: control.visualFocus ? Theme.focusRingWidth : 0
        border.color: Theme.accent
    }

    contentItem: Item {
        implicitWidth: column.implicitWidth
        implicitHeight: column.implicitHeight

        ColumnLayout {
            id: column

            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Theme.spacing.xs

            Label {
                Layout.fillWidth: true
                text: control.text
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            ColumnLayout {
                id: detailsColumn

                Layout.fillWidth: true
                spacing: Theme.spacing.xs
            }
        }
    }
}
