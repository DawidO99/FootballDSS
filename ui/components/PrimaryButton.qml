import QtQuick
import QtQuick.Controls.Basic

// Main call to action: accent background, one per screen area.
Button {
    id: control

    implicitHeight: Math.max(Theme.hitMin + Theme.spacing.xs, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Theme.spacing.l
    rightPadding: Theme.spacing.l
    hoverEnabled: true
    font.pixelSize: Theme.fontSize.body
    font.weight: Font.DemiBold
    Accessible.name: text
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: Theme.radius.m
        color: Theme.accent
        opacity: control.down ? 0.8 : control.hovered ? 0.9 : 1.0
        border.width: control.visualFocus ? 2 : 0
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
