import QtQuick
import QtQuick.Controls.Basic

// The minutes of one row of the training grid. Not a NumberField: that keeps its last valid value when
// text is out of range, and a save would then write something else than what is on the screen. Here
// every edit goes to the owner, which holds the value and reports the error.
// API:
//   value: var              input: int, or null when empty
//   hasError: bool          red border
//   displayText             read-only: the text in the field
//   minutesEdited(string text)    digits only, empty when cleared
//   reverted() (Esc), nextRowRequested() (Enter)
// Set Accessible.name to what the cell is about.
FocusScope {
    id: root

    property var value: null
    property bool hasError: false
    readonly property string displayText: input.text

    signal minutesEdited(string text)
    signal reverted
    signal nextRowRequested

    function textOf(v) {
        return v === null || v === undefined ? "" : String(v);
    }

    // Text being typed stays when it already means this value, or when it is empty: an empty field
    // means the duration of the session, so the value does not say what to show until the user leaves.
    function showValue() {
        if (input.activeFocus && (input.text.length === 0 || Number(input.text) === root.value))
            return;
        input.text = root.textOf(root.value);
    }

    implicitWidth: 2 * Theme.controlHeight
    implicitHeight: Theme.hitMin
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    onValueChanged: root.showValue()
    Component.onCompleted: input.text = root.textOf(root.value)

    TextField {
        id: input

        anchors.fill: parent
        focus: true
        padding: 0
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        inputMethodHints: Qt.ImhDigitsOnly
        maximumLength: 3
        validator: RegularExpressionValidator {
            regularExpression: /[0-9]*/
        }
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        font.pixelSize: Theme.fontSize.body
        hoverEnabled: true
        Accessible.role: Accessible.SpinBox
        Accessible.name: root.Accessible.name
        onTextEdited: root.minutesEdited(text)
        // Typing replaces what was there.
        onActiveFocusChanged: {
            if (activeFocus)
                selectAll();
            else
                text = root.textOf(root.value);
        }
        Keys.onEscapePressed: {
            root.reverted();
            text = root.textOf(root.value);
            selectAll();
        }
        Keys.onReturnPressed: root.nextRowRequested()
        Keys.onEnterPressed: root.nextRowRequested()

        background: FieldBackground {
            active: input.activeFocus
            hasError: root.hasError
            hovered: input.hovered
        }
    }
}
