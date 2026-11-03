import QtQuick
import QtQuick.Controls.Basic

// Destructive action (Delete, Archive all). Single destructive actions run immediately with an
// undo toast; a confirm dialog with a DangerButton is for bulk actions and for deleting an item that
// carries user-entered data (AGENTS.md section 6).
// API: text, touch (44 px hit target), clicked(). Enter/Return click it when focused.
Button {
    id: control

    property bool touch: false

    implicitHeight: Math.max(control.touch ? Theme.controlHeightTouch : Theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Theme.spacing.l
    rightPadding: Theme.spacing.l
    hoverEnabled: true
    font.pixelSize: Theme.fontSize.body
    font.weight: Font.DemiBold
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.name: text
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: Theme.radius.m
        color: Theme.danger
        opacity: control.down ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 1.0
        border.width: control.visualFocus ? Theme.focusRingWidth : 0
        border.color: Theme.text
    }

    contentItem: Label {
        text: control.text
        font: control.font
        color: Theme.accentText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}

