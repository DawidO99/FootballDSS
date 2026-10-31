import QtQuick
import QtQuick.Controls.Basic

// Sidebar entry: icon + label, icon only (with tooltip) when the sidebar is collapsed.
AbstractButton {
    id: control

    property string iconName
    property bool collapsed: false
    property bool current: false
    property bool showFocusRing: visualFocus

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Math.max(Theme.hitMin, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Theme.spacing.m
    rightPadding: Theme.spacing.m
    topPadding: Theme.spacing.s
    bottomPadding: Theme.spacing.s
    hoverEnabled: true

    Accessible.name: text
    ToolTip.visible: collapsed && hovered
    ToolTip.text: text
    ToolTip.delay: 400

    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: Theme.radius.m
        color: control.current || control.hovered ? Theme.surfaceAlt : "transparent"
        border.width: control.showFocusRing ? 2 : 0
        border.color: Theme.accent

        Rectangle {
            visible: control.current
            width: 3
            radius: 2
            color: Theme.accent
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacing.s
            anchors.bottomMargin: Theme.spacing.s
        }
    }

    contentItem: Row {
        spacing: Theme.spacing.m

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            color: control.current ? Theme.accent : Theme.textMuted
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            visible: !control.collapsed
            text: control.text
            color: control.current ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.fontSize.body
            font.weight: control.current ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }
    }
}
