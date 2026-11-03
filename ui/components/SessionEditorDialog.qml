import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Adds or edits a training session. Enter saves from any field. A role without EditCalendar gets the
// same form read-only, and every role can go on to the training sheet of a saved session. That stays
// off while the form has unsaved changes, so nothing is lost on the way.
// API:
//   openForAdding(date), openForEditing(sessionId)
//   saved(double sessionId, string date)
//   deleteRequested(double sessionId)
//   openSheetRequested(double sessionId)
AppDialog {
    id: root

    readonly property alias viewModel: editor

    signal saved(double sessionId, string date)
    signal deleteRequested(double sessionId)
    signal openSheetRequested(double sessionId)

    function showForm() {
        // The same value as last time changes nothing in a field, so text it refused would stay.
        dateInput.showDate();
        startInput.showTime();
        durationInput.showValue();
        root.open();
        dateInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function focusFirstError() {
        if (editor.dateError.length > 0)
            dateInput.forceActiveFocus(Qt.TabFocusReason);
        else if (editor.startTimeError.length > 0)
            startInput.forceActiveFocus(Qt.TabFocusReason);
        else if (editor.durationError.length > 0)
            durationInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function openForAdding(date) {
        editor.startAdding(date);
        root.showForm();
    }

    function openForEditing(sessionId) {
        if (editor.startEditing(sessionId))
            root.showForm();
    }

    title: editor.adding ? qsTr("Add session") : editor.readOnly ? qsTr("Session details") : qsTr("Edit session")
    primaryText: editor.readOnly ? "" : qsTr("Save")
    secondaryText: editor.readOnly ? qsTr("Close") : qsTr("Cancel")
    dangerText: editor.adding || editor.readOnly ? "" : qsTr("Delete session")
    primaryEnabled: dateInput.validationError.length === 0 && !startInput.hasInvalidText && durationInput.validationError.length === 0
    onPrimaryClicked: {
        if (editor.save())
            root.close();
        else
            root.focusFirstError();
    }
    onDangerClicked: root.deleteRequested(editor.sessionId)

    SessionEditorViewModel {
        id: editor

        teamId: AppState.teamId
        onSaved: (sessionId, date) => root.saved(sessionId, date)
        onErrorOccurred: message => Toasts.error(message)
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        rowSpacing: Theme.spacing.m
        columnSpacing: Theme.spacing.l

        FormField {
            id: dateField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Date")
            mandatory: true
            error: dateInput.effectiveError

            DateField {
                id: dateInput

                objectName: "sessionDateInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                date: editor.date
                error: editor.dateError
                Accessible.name: dateField.label
                onDateEdited: date => editor.date = date
            }
        }

        FormField {
            id: startField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Start time")
            error: startInput.effectiveError

            TimeField {
                id: startInput

                objectName: "startTimeInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                time: editor.startTime
                error: editor.startTimeError
                Accessible.name: startField.label
                onTimeEdited: time => editor.startTime = time
            }
        }

        FormField {
            id: durationField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Duration (min)")
            mandatory: true
            error: durationInput.effectiveError

            NumberField {
                id: durationInput

                objectName: "durationInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                from: editor.minDuration
                to: editor.maxDuration
                stepSize: editor.durationStep
                value: editor.durationMin
                error: editor.durationError
                Accessible.name: durationField.label
                onValueModified: value => editor.durationMin = value
            }
        }

        FormField {
            id: typeField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Type")
            mandatory: true

            SelectField {
                id: typeInput

                objectName: "sessionTypeInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                textRole: "text"
                valueRole: "value"
                model: editor.typeOptions
                value: editor.type
                Accessible.name: typeField.label
                onValueActivated: value => editor.type = value
            }
        }
    }

    FormField {
        id: noteField

        Layout.fillWidth: true
        label: qsTr("Note")
        helpText: editor.readOnly ? "" : qsTr("Shift+Enter: new line")

        // Lower than the default, so the form with its errors still fits the minimum window.
        TextAreaField {
            id: noteInput

            objectName: "noteInput"
            Layout.fillWidth: true
            Layout.preferredHeight: 2 * Theme.controlHeight
            enabled: !editor.readOnly
            text: editor.note
            Accessible.name: noteField.label
            onTextChanged: editor.note = text
            onAccepted: root.triggerPrimary()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: !editor.adding
        spacing: Theme.spacing.m

        SecondaryButton {
            objectName: "openSheetButton"
            enabled: !editor.dirty
            text: qsTr("Open training sheet")
            onClicked: root.openSheetRequested(editor.sessionId)
        }

        Label {
            Layout.fillWidth: true
            visible: editor.dirty
            text: qsTr("Save the changes first.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
            wrapMode: Text.WordWrap
        }
    }
}
