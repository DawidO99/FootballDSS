import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One staff member on the profile picker: initials avatar, name and role.
// API:
//   name, roleName, initials: string
//   selected: bool       accent border and a check mark, so it is never color-only
//   clicked()            also Enter and Space when focused
AbstractButton {
    id: root

    property string name
    property string roleName
    property string initials
    property bool selected: false

    implicitWidth: Theme.profileTileWidth
    implicitHeight: Theme.profileTileHeight
    padding: Theme.spacing.m
    hoverEnabled: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("%1, %2").arg(root.name).arg(root.roleName)
    Accessible.checkable: true
    Accessible.checked: root.selected
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: Theme.radius.l
        color: root.selected ? Theme.selection : root.hovered ? Theme.rowHover : Theme.surface
        border.width: root.selected || root.visualFocus ? Theme.focusRingWidth : Theme.borderWidth
        border.color: root.selected || root.visualFocus ? Theme.accent : Theme.border

        Icon {
            objectName: "selectedMark"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spacing.s
            visible: root.selected
            name: "ok"
            size: Theme.iconSize.m
            color: Theme.accent
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing.xs

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Theme.avatarSize
            Layout.preferredHeight: Theme.avatarSize
            radius: Theme.avatarSize / 2
            color: Theme.accent

            Label {
                anchors.centerIn: parent
                text: root.initials
                textFormat: Text.PlainText
                color: Theme.accentText
                font.pixelSize: Theme.fontSize.title
                font.weight: Font.DemiBold
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.name
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            text: root.roleName
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
}
