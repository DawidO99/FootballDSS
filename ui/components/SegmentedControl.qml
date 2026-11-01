pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// 2-5 mutually exclusive options in one row (filters, view modes, attendance status).
// Left/Right move the selection while the control has keyboard focus.
// API:
//   model: var           [{ value, text }], 2-5 entries
//   currentValue: var    value of the selected option (undefined = none)
//   currentIndex         read-only
//   touch: bool          44 px hit targets
//   activated(var value) the user picked an option (`currentValue` is already updated)
// Set Accessible.name to what the options choose (e.g. "Status filter").
FocusScope {
    id: root

    property var model: []
    property var currentValue
    property bool touch: false
    readonly property int currentIndex: indexOfValue(currentValue)

    signal activated(var value)

    function indexOfValue(value) {
        for (let i = 0; i < root.model.length; ++i) {
            if (root.model[i].value === value)
                return i;
        }
        return -1;
    }

    function select(index) {
        if (index < 0 || index >= root.model.length || index === root.currentIndex)
            return;
        root.currentValue = root.model[index].value;
        root.activated(root.currentValue);
    }

    implicitWidth: row.implicitWidth + 2 * Theme.borderWidth
    implicitHeight: (touch ? Theme.controlHeightTouch : Theme.controlHeight)
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.role: Accessible.Grouping

    Keys.onLeftPressed: root.select(root.currentIndex < 0 ? 0 : root.currentIndex - 1)
    Keys.onRightPressed: root.select(root.currentIndex + 1)

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.m
        color: Theme.surfaceAlt
        border.width: root.activeFocus ? Theme.focusRingWidth : Theme.borderWidth
        border.color: root.activeFocus ? Theme.accent : Theme.border
    }

    Row {
        id: row

        anchors.fill: parent
        anchors.margins: Theme.focusRingWidth

        Repeater {
            model: root.model

            delegate: AbstractButton {
                id: segment

                required property var modelData
                required property int index
                readonly property bool selected: index === root.currentIndex

                height: row.height
                implicitWidth: Math.max(root.touch ? Theme.hitTouch : Theme.hitMin, label.implicitWidth + 2 * Theme.spacing.l)
                text: modelData.text
                checkable: true
                checked: selected
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                Accessible.role: Accessible.RadioButton
                Accessible.name: text
                onClicked: {
                    root.forceActiveFocus(Qt.MouseFocusReason);
                    root.select(index);
                }

                background: Rectangle {
                    radius: Theme.radius.s
                    color: segment.selected ? Theme.accent : segment.hovered ? Theme.rowHover : "transparent"
                }

                contentItem: Label {
                    id: label

                    text: segment.text
                    color: segment.selected ? Theme.accentText : Theme.text
                    font.pixelSize: Theme.fontSize.body
                    font.weight: segment.selected ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}

