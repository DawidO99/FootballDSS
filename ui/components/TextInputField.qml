import QtQuick
import QtQuick.Controls.Basic

// Single-line text input with an error state.
// API:
//   text, placeholderText, validator, maximumLength, readOnly   (inherited from TextField)
//   textEdited(), editingFinished(), accepted()                 (inherited; user edits only)
//   error: string            external message (e.g. from the viewmodel), empty = valid
//   invalidMessage: string   shown when the validator rejects the text
//   validationError          read-only, own check: validator rejected the text (after leaving)
//   effectiveError           read-only, validationError or error; drives the red border
// Set Accessible.name to the field label (defaults to the placeholder).
TextField {
    id: control

    property string error
    property string invalidMessage: qsTr("Invalid value")
    readonly property string validationError: !acceptableInput && text.length > 0 && !activeFocus ? invalidMessage : ""
    readonly property string effectiveError: validationError.length > 0 ? validationError : error

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacing.m
    rightPadding: Theme.spacing.m
    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.pixelSize: Theme.fontSize.body
    hoverEnabled: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.name: placeholderText

    background: FieldBackground {
        active: control.activeFocus
        hasError: control.effectiveError.length > 0
        hovered: control.hovered
    }
}

