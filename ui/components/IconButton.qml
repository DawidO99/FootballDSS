import QtQuick
import QtQuick.Controls.Basic

// Square icon-only button. `text` is mandatory: it is the tooltip and the accessible name.
// API: iconName (see Icon.qml), text, iconColor, touch (44 px hit target), clicked().
// Enter/Return click it when focused.
AbstractButton {
    id: control

    property string iconName
    property color iconColor: Theme.text
    property bool touch: false

    readonly property int side: touch ? Theme.controlHeightTouch : Theme.controlHeight

    implicitWidth: side
    implicitHeight: side
    hoverEnabled: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.role: Accessible.Button
    Accessible.name: text
    ToolTip.visible: hovered && text.length > 0
    ToolTip.text: text
    ToolTip.delay: Theme.tooltipDelay
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: Theme.radius.m
        color: control.down ? Theme.border : control.hovered ? Theme.surfaceAlt : "transparent"
        border.width: control.visualFocus ? Theme.focusRingWidth : 0
        border.color: Theme.accent
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            color: control.iconColor
            size: Theme.iconSize.m
        }
    }
}

