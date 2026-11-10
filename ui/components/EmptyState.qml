import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shown instead of content when there is nothing to display; always offers a next step.
Item {
    id: root

    property string title: qsTr("Nothing here yet")
    property string description
    property string actionText
    property string iconName: "inbox"

    signal actionTriggered

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column

        anchors.centerIn: parent
        width: Math.min(root.width, Theme.emptyStateWidth)
        spacing: Theme.spacing.m

        Icon {
            Layout.alignment: Qt.AlignHCenter
            name: root.iconName
            size: Theme.iconSize.l
            color: Theme.textMuted
        }

        Label {
            Layout.fillWidth: true
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontSize.title
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.description
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.body
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        PrimaryButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacing.s
            visible: text.length > 0
            text: root.actionText
            onClicked: root.actionTriggered()
        }
    }
}
