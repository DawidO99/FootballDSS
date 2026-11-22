import QtQuick
import QtQuick.Layouts
import FootballDSS

// Adds or edits a player. Enter saves from any field, the secondary positions list excepted while it
// is open. Save stays off while a number field holds text it rejected: the viewmodel still has the
// last valid value, which must not be saved in its place.
// API:
//   openForAdding(), openForEditing(playerId)
//   saved(double playerId)
AppDialog {
    id: root

    readonly property alias viewModel: editor

    signal saved(double playerId)

    function showForm() {
        // The same value as last time changes nothing in a field, so text it refused would stay.
        shirtNumberInput.showValue();
        birthYearInput.showValue();
        root.open();
        firstNameInput.forceActiveFocus(Qt.TabFocusReason);
    }

    // Enter may come from the notes, with the first error scrolled out of sight above.
    function focusFirstError() {
        if (editor.firstNameError.length > 0)
            firstNameInput.forceActiveFocus(Qt.TabFocusReason);
        else if (editor.lastNameError.length > 0)
            lastNameInput.forceActiveFocus(Qt.TabFocusReason);
        else if (editor.shirtNumberError.length > 0)
            shirtNumberInput.forceActiveFocus(Qt.TabFocusReason);
        else if (editor.birthYearError.length > 0)
            birthYearInput.forceActiveFocus(Qt.TabFocusReason);
        else if (editor.secondaryPositionsError.length > 0)
            secondaryPositionsInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function openForAdding() {
        editor.startAdding();
        root.showForm();
    }

    function openForEditing(playerId) {
        if (editor.startEditing(playerId))
            root.showForm();
    }

    title: editor.adding ? qsTr("Add player") : qsTr("Edit player")
    primaryEnabled: shirtNumberInput.validationError.length === 0 && birthYearInput.validationError.length === 0
    onPrimaryClicked: {
        if (editor.save())
            root.close();
        else
            root.focusFirstError();
    }

    PlayerEditorViewModel {
        id: editor

        teamId: AppState.teamId
        onSaved: playerId => root.saved(playerId)
        onErrorOccurred: message => Toasts.error(message)
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        rowSpacing: Theme.spacing.m
        columnSpacing: Theme.spacing.l

        FormField {
            id: firstNameField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("First name")
            mandatory: true
            error: firstNameInput.effectiveError

            TextInputField {
                id: firstNameInput

                Layout.fillWidth: true
                text: editor.firstName
                error: editor.firstNameError
                Accessible.name: firstNameField.label
                onTextEdited: editor.firstName = text
            }
        }

        FormField {
            id: lastNameField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Last name")
            mandatory: true
            error: lastNameInput.effectiveError

            TextInputField {
                id: lastNameInput

                Layout.fillWidth: true
                text: editor.lastName
                error: editor.lastNameError
                Accessible.name: lastNameField.label
                onTextEdited: editor.lastName = text
            }
        }

        FormField {
            id: shirtNumberField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Shirt number")
            error: shirtNumberInput.effectiveError

            NumberField {
                id: shirtNumberInput

                Layout.fillWidth: true
                from: editor.minShirtNumber
                to: editor.maxShirtNumber
                optional: true
                value: editor.shirtNumber
                error: editor.shirtNumberError
                Accessible.name: shirtNumberField.label
                onValueModified: value => editor.shirtNumber = value
            }
        }

        FormField {
            id: birthYearField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Birth year")
            error: birthYearInput.effectiveError

            NumberField {
                id: birthYearInput

                objectName: "birthYearInput"
                Layout.fillWidth: true
                from: editor.minBirthYear
                to: editor.maxBirthYear
                optional: true
                value: editor.birthYear
                error: editor.birthYearError
                Accessible.name: birthYearField.label
                onValueModified: value => editor.birthYear = value
            }
        }

        FormField {
            id: primaryPositionField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Primary position")
            // P19 decision 8: "—" when it is not known yet.

            SelectField {
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: editor.positionOptions
                value: editor.primaryPosition
                Accessible.name: primaryPositionField.label
                onValueActivated: value => editor.primaryPosition = value
            }
        }

        FormField {
            id: secondaryPositionsField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Secondary positions")
            error: secondaryPositionsInput.effectiveError

            MultiSelectField {
                id: secondaryPositionsInput

                Layout.fillWidth: true
                model: editor.secondaryPositionOptions
                values: editor.secondaryPositions
                placeholderText: qsTr("None")
                error: editor.secondaryPositionsError
                Accessible.name: secondaryPositionsField.label
                onValuesEdited: values => editor.secondaryPositions = values
            }
        }

        FormField {
            id: footField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Preferred foot")

            SelectField {
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: editor.footOptions
                value: editor.foot
                Accessible.name: footField.label
                onValueActivated: value => editor.foot = value
            }
        }
    }

    FormField {
        id: notesField

        Layout.fillWidth: true
        label: qsTr("Notes")
        helpText: qsTr("Shift+Enter: new line")

        // Lower than the default, so the form with its errors still fits the minimum window.
        TextAreaField {
            Layout.fillWidth: true
            Layout.preferredHeight: 2 * Theme.controlHeight
            text: editor.notes
            Accessible.name: notesField.label
            onTextChanged: editor.notes = text
            onAccepted: root.triggerPrimary()
        }
    }
}
