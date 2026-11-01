import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Whole-number input: only digits can be typed ('-' too when `from` is negative), Up/Down keys and
// the -/+ buttons step by `stepSize` within from..to. Out-of-range or empty mandatory text shows an
// error and leaves `value` unchanged; nothing is clamped silently.
// API:
//   value: var              int, or null when empty
//   from, to, stepSize: int
//   optional: bool          empty text is a valid "no value" (null)
//   placeholderText, error (external message), touch (44 px buttons)
//   validationError, effectiveError   read-only, see TextInputField
//   valueModified(var value)          user changed the value (typing, keys or buttons)
//   increase(), decrease()
// User edits assign `value` (like Qt Quick Controls); keep a viewmodel in sync in valueModified.
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
        if (root.isEmpty(v) && root.isEmpty(root.value))
            return;
        if (v === root.value)
            return;
        root.value = v;
        root.valueModified(v);
    }

    function stepBy(delta) {
        const next = root.isEmpty(root.value) ? Math.min(Math.max(0, root.from), root.to) : Math.min(Math.max(root.value + delta, root.from), root.to);
        input.text = root.textFor(next);
        internal.validationError = "";
        root.setValue(next);
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
        const number = Number(text);
        if (!Number.isInteger(number)) {
            internal.validationError = qsTr("Enter a whole number.");
            return;
        }
        if (number < root.from || number > root.to) {
            internal.validationError = qsTr("Enter a number from %1 to %2.").arg(root.from).arg(root.to);
            return;
        }
        internal.validationError = "";
        root.setValue(number);
    }

    implicitWidth: Theme.fieldWidth / 2 + 2 * buttonSide
    implicitHeight: buttonSide
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    onValueChanged: {
        // External change: show it. Own changes already match the text.
        if (input.text.length === 0 || Number(input.text) !== root.value) {
            input.text = root.textFor(root.value);
            internal.validationError = "";
        }
    }
    Component.onCompleted: input.text = root.textFor(root.value)

    QtObject {
        id: internal

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
            enabled: root.isEmpty(root.value) || root.value > root.from
            onClicked: root.decrease()
        }

        TextField {
            id: input

            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            maximumLength: Math.max(String(root.from).length, String(root.to).length)
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
                    text = root.textFor(root.value);
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
            enabled: root.isEmpty(root.value) || root.value < root.to
            onClicked: root.increase()
        }
    }
}
