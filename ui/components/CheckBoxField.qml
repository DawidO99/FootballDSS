import QtQuick
import QtQuick.Controls.Basic

// Checkbox with a label. Space toggles it; Enter is passed on (e.g. to save a dialog). A tick shows
// the state, so it is never color-only.
// API:
//   text, checked, toggled()     (inherited from CheckBox)
//   highlighted: bool            shows the row as the keyboard's current one in a list
// A binding on `checked` survives a click. Keep a viewmodel in sync in toggled.
CheckBox {
    id: control

    property bool highlighted: false

    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacing.s
    rightPadding: Theme.spacing.s
    spacing: Theme.spacing.s
    hoverEnabled: true
    font.pixelSize: Theme.fontSize.body
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.name: text

    background: Rectangle {
        radius: Theme.radius.s
        color: control.highlighted ? Theme.selection : control.hovered ? Theme.rowHover : "transparent"
        border.width: control.visualFocus ? Theme.focusRingWidth : 0
        border.color: Theme.accent
    }

    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        implicitWidth: Theme.iconSize.m
        implicitHeight: Theme.iconSize.m
        radius: Theme.radius.s
        color: control.checked ? Theme.accent : "transparent"
        border.width: Theme.borderWidth
        border.color: control.checked ? Theme.accent : Theme.textMuted

        Icon {
            anchors.centerIn: parent
            visible: control.checked
            name: "check"
            size: Theme.iconSize.s
            color: Theme.accentText
        }
    }

    contentItem: Label {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
