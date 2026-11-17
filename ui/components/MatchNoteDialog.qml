import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A note of Match Day with its minute (packet P13): add or edit. The minute is any minute already
// played, written as on the timeline ("23", "45+2"); a player is optional; the text 1–500
// characters, shown as plain text.
// API:
//   openForAdding(minute)                          the last minute played
//   openForEditing(noteId, minute, playerId, text)
//   playerOptions: var       [{ value: playerId or 0, text }]
//   minuteError: string      the check of the minute, bound by the caller to minuteText
//   minuteText: string       read-only, what is typed
//   saveRequested(double noteId, string minute, double playerId, string text)
//                            noteId 0: a new note; the caller closes the dialog when it was saved
AppDialog {
    id: root

    property var playerOptions: []
    property string minuteError
    property double noteId: 0
    property double playerId: 0
    readonly property alias minuteText: minuteField.text
    readonly property alias minuteInput: minuteField
    readonly property alias textInput: textField
    readonly property int maxLength: 500
    readonly property bool textTooLong: textField.text.trim().length > root.maxLength

    signal saveRequested(double noteId, string minute, double playerId, string text)

    function openForAdding(minute) {
        root.noteId = 0;
        root.playerId = 0;
        minuteField.text = minute;
        textField.text = "";
        playerField.showValue();
        root.open();
        textField.forceInputFocus();
    }

    function openForEditing(noteId, minute, playerId, text) {
        root.noteId = noteId;
        root.playerId = playerId;
        minuteField.text = minute;
        textField.text = text;
        playerField.showValue();
        root.open();
        textField.forceInputFocus();
    }

    size: AppDialog.Medium
    touch: true
    title: root.noteId > 0 ? qsTr("Edit note") : qsTr("Add note")
    primaryText: qsTr("Save note")
    primaryEnabled: root.minuteError.length === 0 && textField.text.trim().length > 0 && !root.textTooLong
    onPrimaryClicked: root.saveRequested(root.noteId, minuteField.text, root.playerId, textField.text)

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing.m

        FormField {
            id: minuteFormField

            Layout.preferredWidth: Theme.fieldWidth / 2
            label: qsTr("Minute")
            mandatory: true
            error: root.minuteError

            TextInputField {
                id: minuteField

                objectName: "noteMinuteField"
                implicitWidth: Theme.matchMinuteFieldWidth
                implicitHeight: Theme.hitTouch
                maximumLength: 7
                error: root.minuteError
                Accessible.name: minuteFormField.label
                onAccepted: root.triggerPrimary()
            }
        }

        FormField {
            id: playerFormField

            Layout.fillWidth: true
            label: qsTr("Player (optional)")

            SelectField {
                id: playerField

                objectName: "notePlayerField"
                touch: true
                model: root.playerOptions
                textRole: "text"
                valueRole: "value"
                value: root.playerId
                Accessible.name: playerFormField.label
                onValueActivated: value => root.playerId = value
            }
        }
    }

    FormField {
        id: textFormField

        Layout.fillWidth: true
        label: qsTr("Note")
        mandatory: true
        helpText: qsTr("%1 / %2 characters").arg(textField.text.trim().length).arg(root.maxLength)
        error: root.textTooLong ? qsTr("At most %1 characters.").arg(root.maxLength) : ""

        TextAreaField {
            id: textField

            objectName: "noteTextField"
            Layout.fillWidth: true
            error: textFormField.error
            Accessible.name: textFormField.label
            onAccepted: root.triggerPrimary()
        }
    }
}
