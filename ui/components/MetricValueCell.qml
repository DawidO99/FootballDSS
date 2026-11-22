import QtQuick
import QtQuick.Controls.Basic

// The value of one row of MetricValueGrid. Every edit goes to the owner, which keeps the text as
// typed and reports what is wrong with it, so a save never writes something else than what is on
// the screen. Digits, a sign and one decimal comma or point.
// API:
//   value: string           input: the text of the viewmodel
//   hasError: bool          red border
//   displayText             read-only: the text in the field
//   valueEdited(string text), reverted() (Esc), nextRowRequested() (Enter)
// Set Accessible.name to what the cell is about.
FocusScope {
    id: root

    property string value
    property bool hasError: false
    readonly property string displayText: input.text

    signal valueEdited(string text)
    signal reverted
    signal nextRowRequested

    implicitWidth: 3 * Theme.controlHeight
    implicitHeight: Theme.hitMin
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    onValueChanged: {
        if (input.text !== root.value)
            input.text = root.value;
    }
    Component.onCompleted: input.text = root.value

    TextField {
        id: input

        anchors.fill: parent
        focus: true
        padding: 0
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        maximumLength: 18
        validator: RegularExpressionValidator {
            regularExpression: /[-+]?[0-9]*([.,][0-9]*)?/
        }
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        font.pixelSize: Theme.fontSize.body
        hoverEnabled: true
        Accessible.role: Accessible.EditableText
        Accessible.name: root.Accessible.name
        onTextEdited: root.valueEdited(text)
        // Typing replaces what was there.
        onActiveFocusChanged: {
            if (activeFocus)
                selectAll();
        }
        Keys.onEscapePressed: {
            root.reverted();
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
