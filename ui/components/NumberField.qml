import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Whole-number input: only digits can be typed ('-' too when `from` is negative), Up/Down keys and
// the -/+ buttons step by `stepSize` within from..to. Out-of-range or empty mandatory text shows an
// error and leaves the value unchanged; nothing is clamped silently. Changing from/to validates the
// text again.
// API:
//   value: var              input: int, or null when empty; never assigned by the field
//   from, to, stepSize: int
//   optional: bool          empty text is a valid "no value" (null)
//   placeholderText, error (external message), touch (44 px buttons)
//   currentValue            read-only: `value`, or the user's last valid edit since `value` changed
//   displayText             read-only: the text in the field
//   validationError, effectiveError   read-only, see TextInputField
//   valueModified(var value)          user changed the value (typing, keys or buttons)
//   increase(), decrease()
// Edits stay inside the field, so a binding on `value` survives them. Keep a viewmodel in sync in
// valueModified; whenever `value` changes the field shows it again.
// Set Accessible.name to the field label.
FocusScope {
    id: root

    property var value: null
    property int from: 0
    property int to: 99
    property int stepSize: 1
    property bool optional: false
    property string placeholderText
    property string error
    property bool touch: false
    readonly property var currentValue: internal.currentValue
    readonly property string displayText: input.text
    readonly property string validationError: internal.validationError
    readonly property string effectiveError: validationError.length > 0 ? validationError : error
    readonly property int buttonSide: touch ? Theme.controlHeightTouch : Theme.controlHeight

    signal valueModified(var value)

    function increase() {
        root.stepBy(root.stepSize);
    }

    function decrease() {
        root.stepBy(-root.stepSize);
    }

    function isEmpty(v) {
        return v === null || v === undefined;
    }

    function textFor(v) {
        return root.isEmpty(v) ? "" : String(v);
    }

    function setValue(v) {
        if (root.isEmpty(v) && root.isEmpty(internal.currentValue))
            return;
        if (v === internal.currentValue)
            return;
        internal.currentValue = v;
        root.valueModified(v);
    }

    function stepBy(delta) {
        const current = internal.currentValue;
        const next = root.isEmpty(current) ? Math.min(Math.max(0, root.from), root.to) : Math.min(Math.max(current + delta, root.from), root.to);
        input.text = root.textFor(next);
        internal.validationError = "";
        root.setValue(next);
    }

    // Error message for non-empty text, empty when it is a valid number in range.
    function errorFor(text) {
        const number = Number(text);
        if (!Number.isInteger(number))
            return qsTr("Enter a whole number.");
        if (number < root.from || number > root.to)
            return qsTr("Enter a number from %1 to %2.").arg(root.from).arg(root.to);
        return "";
    }

    function commit(text) {
        if (text.length === 0) {
            if (root.optional) {
                internal.validationError = "";
                root.setValue(null);
            } else {
                internal.validationError = qsTr("Enter a number.");
            }
            return;
        }
        internal.validationError = root.errorFor(text);
        if (internal.validationError.length === 0)
            root.setValue(Number(text));
    }

    // The outside value replaces the field's state. Text that already means that value stays, so a
    // viewmodel echoing "007" as 7 does not rewrite what the user is typing.
    function showValue() {
        internal.currentValue = root.value;
        if (input.text.length === 0 || Number(input.text) !== root.value)
            input.text = root.textFor(root.value);
        internal.validationError = input.text.length > 0 ? root.errorFor(input.text) : "";
    }

    // The limits changed. An untouched empty field stays valid; text that was out of range and now
    // fits is the user's edit, so it is committed like any other.
    function revalidate() {
        if (internal.completed && input.text.length > 0)
            root.commit(input.text);
    }

    implicitWidth: Theme.fieldWidth / 2 + 2 * buttonSide
    implicitHeight: buttonSide
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    onValueChanged: {
        if (internal.completed)
            root.showValue();
    }
    onFromChanged: root.revalidate()
    onToChanged: root.revalidate()
    Component.onCompleted: {
        internal.completed = true;
        root.showValue();
    }

    QtObject {
        id: internal

        property bool completed: false
        property var currentValue: null
        property string validationError
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing.xs

        IconButton {
            iconName: "minus"
            text: qsTr("Decrease")
            touch: root.touch
            focusPolicy: Qt.NoFocus
            enabled: root.isEmpty(internal.currentValue) || internal.currentValue > root.from
            onClicked: root.decrease()
        }

        TextField {
            id: input

            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: RegularExpressionValidator {
                regularExpression: root.from < 0 ? /-?[0-9]*/ : /[0-9]*/
            }
            placeholderText: root.placeholderText
            placeholderTextColor: Theme.textMuted
            color: Theme.text
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            font.pixelSize: Theme.fontSize.body
            hoverEnabled: true
            Accessible.role: Accessible.SpinBox
            Accessible.name: root.Accessible.name
            onTextEdited: root.commit(text)
            // Normalise "007" to "7" once the user is done; invalid text stays for correction.
            onEditingFinished: {
                if (root.validationError.length === 0)
                    text = root.textFor(internal.currentValue);
            }
            Keys.onUpPressed: root.increase()
            Keys.onDownPressed: root.decrease()

            background: FieldBackground {
                active: input.activeFocus
                hasError: root.effectiveError.length > 0
                hovered: input.hovered
            }
        }

        IconButton {
            iconName: "plus"
            text: qsTr("Increase")
            touch: root.touch
            focusPolicy: Qt.NoFocus
            enabled: root.isEmpty(internal.currentValue) || internal.currentValue < root.to
            onClicked: root.increase()
        }
    }
}
