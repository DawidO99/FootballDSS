import QtQuick
import QtQuick.Controls.Basic

// One RPE of the training grid. Holds no value of its own: a digit goes to the owner, which decides
// about 1 + 0 = 10, and comes back through `value`. A click gives it the focus, so a mouse reaches it
// too.
// API:
//   value: var              input: int, or null when empty
//   digitEntered(int digit), cleared() (Delete, Backspace), reverted() (Esc),
//   nextRowRequested() (Enter)
// Set Accessible.name to what the cell is about.
Item {
    id: root

    property var value: null

    signal digitEntered(int digit)
    signal cleared
    signal reverted
    signal nextRowRequested

    implicitWidth: 2 * Theme.controlHeight
    implicitHeight: Theme.hitMin
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.role: Accessible.SpinBox

    Keys.onPressed: event => {
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return;
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            root.digitEntered(event.key - Qt.Key_0);
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            root.cleared();
        } else if (event.key === Qt.Key_Escape) {
            root.reverted();
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.nextRowRequested();
        } else {
            return;
        }
        event.accepted = true;
    }

    FieldBackground {
        anchors.fill: parent
        active: root.activeFocus
        hovered: hover.hovered
    }

    Label {
        anchors.fill: parent
        text: root.value === null || root.value === undefined ? "" : String(root.value)
        textFormat: Text.PlainText
        color: Theme.text
        font.pixelSize: Theme.fontSize.body
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    HoverHandler {
        id: hover
    }

    TapHandler {
        onTapped: root.forceActiveFocus(Qt.MouseFocusReason)
    }
}
