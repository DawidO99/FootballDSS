pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// 2-5 mutually exclusive options in one row (filters, view modes, attendance status).
// Left/Right move the selection while the control has keyboard focus.
// API:
//   model: var           [{ value, text }], 2-5 entries
//   currentValue: var    input: value of the selected option (undefined = none); never assigned by
//                        the control
//   currentIndex         read-only: the shown option, `currentValue` or the user's last pick
//   touch: bool          44 px hit targets
//   activated(var value) the user picked an option
// A pick stays inside the control, so a binding on `currentValue` survives it. Keep a viewmodel in
// sync in activated; whenever `currentValue` changes the control shows it again.
// Set Accessible.name to what the options choose (e.g. "Status filter").
FocusScope {
    id: root

    property var model: []
    property var currentValue
    property bool touch: false
    readonly property int currentIndex: indexOfValue(internal.selectedValue)

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
        internal.selectedValue = root.model[index].value;
        root.activated(internal.selectedValue);
    }

    // The segments sit inside the focus ring, so the hit target decides the control's size.
    implicitWidth: row.implicitWidth + 2 * Theme.focusRingWidth
    implicitHeight: (touch ? Theme.hitTouch : Theme.hitMin) + 2 * Theme.focusRingWidth
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.role: Accessible.Grouping

    onCurrentValueChanged: internal.selectedValue = root.currentValue
    Component.onCompleted: internal.selectedValue = root.currentValue

    Keys.onLeftPressed: root.select(root.currentIndex < 0 ? 0 : root.currentIndex - 1)
    Keys.onRightPressed: root.select(root.currentIndex + 1)

    QtObject {
        id: internal

        property var selectedValue
    }

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
            // By count: retranslating a qsTr() model must not rebuild the segments.
            model: root.model.length

            delegate: AbstractButton {
                id: segment

                required property int index
                readonly property bool selected: index === root.currentIndex

                objectName: "segment" + index
                height: row.height
                implicitWidth: Math.max(root.touch ? Theme.hitTouch : Theme.hitMin, label.implicitWidth + 2 * Theme.spacing.l)
                text: root.model[index]?.text ?? ""
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

