import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Surface container with an optional header (title + right-aligned actions) and a body slot.
// API:
//   title: string
//   content (default): body items placed in a vertical ColumnLayout
//   actions: items placed on the right side of the header row
Rectangle {
    id: root

    property string title
    default property alias content: bodyColumn.data
    property alias actions: actionsRow.data

    implicitWidth: cardColumn.implicitWidth + 2 * Theme.spacing.l
    implicitHeight: cardColumn.implicitHeight + 2 * Theme.spacing.l
    color: Theme.surface
    radius: Theme.radius.m
    border.width: Theme.borderWidth
    border.color: Theme.border

    ColumnLayout {
        id: cardColumn

        anchors.fill: parent
        anchors.margins: Theme.spacing.l
        spacing: Theme.spacing.m

        RowLayout {
            Layout.fillWidth: true
            visible: root.title.length > 0 || actionsRow.children.length > 0
            spacing: Theme.spacing.m

            Label {
                Layout.fillWidth: true
                visible: root.title.length > 0
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontSize.title
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Item {
                Layout.fillWidth: true
                visible: root.title.length === 0
            }

            RowLayout {
                id: actionsRow

                spacing: Theme.spacing.s
            }
        }

        ColumnLayout {
            id: bodyColumn

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing.m
        }
    }
}

