import QtQuick
import QtQuick.Controls.Basic

// Multi-line text input for forms. Enter means the same as in the single-line fields, so it emits
// accepted() (connect it to AppDialog.triggerPrimary()); Shift+Enter breaks the line. Tab leaves
// the field.
// API:
//   text: string, placeholderText
//   error: string            external message, empty = valid (red border)
//   effectiveError           read-only
//   accepted()               Enter pressed
//   forceInputFocus()
// Set Accessible.name to the field label.
ScrollView {
    id: root

    property alias text: area.text
    property alias placeholderText: area.placeholderText
    property string error
    readonly property string effectiveError: error

    signal accepted

    function forceInputFocus() {
        area.forceActiveFocus(Qt.TabFocusReason);
    }

    implicitWidth: Theme.fieldWidth
    implicitHeight: 3 * Theme.controlHeight
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    TextArea {
        id: area

        focus: true
        activeFocusOnTab: true
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        leftPadding: Theme.spacing.m
        rightPadding: Theme.spacing.m
        topPadding: Theme.spacing.s
        bottomPadding: Theme.spacing.s
        color: Theme.text
        placeholderTextColor: Theme.textMuted
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        font.pixelSize: Theme.fontSize.body
        hoverEnabled: true
        Accessible.role: Accessible.EditableText
        Accessible.name: root.Accessible.name
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (event.modifiers & Qt.ShiftModifier) {
                    // The default would insert a line separator, not a new line.
                    area.remove(area.selectionStart, area.selectionEnd);
                    area.insert(area.cursorPosition, "\n");
                } else {
                    root.accepted();
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                area.nextItemInFocusChain(event.key === Qt.Key_Tab).forceActiveFocus(Qt.TabFocusReason);
                event.accepted = true;
            }
        }

        background: FieldBackground {
            active: area.activeFocus
            hasError: root.effectiveError.length > 0
            hovered: area.hovered
        }
    }
}
