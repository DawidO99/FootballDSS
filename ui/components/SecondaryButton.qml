import QtQuick
import QtQuick.Controls.Basic

// Secondary action next to a PrimaryButton (Cancel, Back, Undo): outlined, no fill.
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
        color: control.down ? Theme.border : control.hovered ? Theme.surfaceAlt : "transparent"
        border.width: control.visualFocus ? Theme.focusRingWidth : Theme.borderWidth
        border.color: control.visualFocus ? Theme.accent : Theme.border
    }

    contentItem: Label {
        text: control.text
        font: control.font
        color: Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}

