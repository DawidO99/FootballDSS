import QtQuick
import QtQuick.Layouts
import FootballDSS

// Adds or edits an availability entry. The description is there only for a role that may read it;
// without it the viewmodel leaves the stored text alone. The return date shows only on an entry that
// has one, so a mistake can be corrected or taken back.
// API:
//   viewModel: AvailabilityViewModel
//   openForAdding()
//   openForEditing(entry)   a row of AvailabilityListModel
AppDialog {
    id: root

    property AvailabilityViewModel viewModel
    // 0: adding a new entry.
    property double entryId: 0
    property string typeKey
    property string fromDate
    property string expectedTo
    property string actualTo
    property string descriptionText
    readonly property bool adding: entryId === 0

    function showForm() {
        root.viewModel.clearErrors();
        // The same date as last time changes nothing in a field, so an edit left there would stay.
        fromInput.showDate();
        expectedInput.showDate();
        actualInput.showDate();
        root.open();
        typeInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function openForAdding() {
        root.entryId = 0;
        root.typeKey = "injury";
        root.fromDate = root.viewModel.today;
        root.expectedTo = "";
        root.actualTo = "";
        root.descriptionText = "";
        descriptionInput.text = "";
        root.showForm();
    }

    function openForEditing(entry) {
        root.entryId = entry.entryId;
        root.typeKey = entry.type;
        root.fromDate = entry.fromDate;
        root.expectedTo = entry.expectedTo;
        root.actualTo = entry.actualTo;
        root.descriptionText = entry.description;
        descriptionInput.text = entry.description;
        root.showForm();
    }

    title: adding ? qsTr("Add availability") : qsTr("Edit availability")
    primaryEnabled: fromInput.validationError.length === 0 && expectedInput.validationError.length === 0 && actualInput.validationError.length === 0
    onPrimaryClicked: {
        const done = root.adding ? root.viewModel.add(root.typeKey, fromInput.currentDate, expectedInput.currentDate, descriptionInput.text) : root.viewModel.update(root.entryId, root.typeKey, fromInput.currentDate, expectedInput.currentDate, actualInput.currentDate, descriptionInput.text);
        if (done)
            root.close();
        else if (root.viewModel.fromDateError.length > 0)
            fromInput.forceActiveFocus(Qt.TabFocusReason);
        else if (root.viewModel.expectedToError.length > 0)
            expectedInput.forceActiveFocus(Qt.TabFocusReason);
        else if (root.viewModel.actualToError.length > 0)
            actualInput.forceActiveFocus(Qt.TabFocusReason);
    }

    FormField {
        id: typeField

        Layout.fillWidth: true
        label: qsTr("Type")
        mandatory: true

        SelectField {
            id: typeInput

            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: root.viewModel ? root.viewModel.typeOptions : []
            value: root.typeKey
            Accessible.name: typeField.label
            onValueActivated: value => root.typeKey = value
        }
    }

    FormField {
        id: fromField

        Layout.fillWidth: true
        label: qsTr("From")
        mandatory: true
        error: fromInput.effectiveError

        DateField {
            id: fromInput

            Layout.fillWidth: true
            date: root.fromDate
            error: root.viewModel ? root.viewModel.fromDateError : ""
            Accessible.name: fromField.label
        }
    }

    FormField {
        id: expectedField

        Layout.fillWidth: true
        label: qsTr("Expected return")
        error: expectedInput.effectiveError

        DateField {
            id: expectedInput

            Layout.fillWidth: true
            date: root.expectedTo
            minimumDate: fromInput.currentDate
            optional: true
            error: root.viewModel ? root.viewModel.expectedToError : ""
            Accessible.name: expectedField.label
        }
    }

    FormField {
        id: actualField

        Layout.fillWidth: true
        visible: root.actualTo.length > 0
        label: qsTr("Available again from")
        helpText: qsTr("Leave it empty if the player has not returned after all.")
        error: actualInput.effectiveError

        DateField {
            id: actualInput

            Layout.fillWidth: true
            date: root.actualTo
            minimumDate: fromInput.currentDate
            optional: true
            error: root.viewModel ? root.viewModel.actualToError : ""
            Accessible.name: actualField.label
        }
    }

    FormField {
        id: descriptionField

        Layout.fillWidth: true
        visible: root.viewModel ? root.viewModel.canViewDetails : false
        label: qsTr("Description")
        helpText: qsTr("Only the coach and the physio can read this. Shift+Enter: new line")

        TextAreaField {
            id: descriptionInput

            Layout.fillWidth: true
            Accessible.name: descriptionField.label
            onAccepted: root.triggerPrimary()
        }
    }
}
