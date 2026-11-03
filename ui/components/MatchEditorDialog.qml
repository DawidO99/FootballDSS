import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Adds or edits a match, and adds an opponent from inside the form. Enter saves from any field; in
// the new opponent field it adds the opponent, and Esc there leaves that field only. A match that is
// not planned, or a role without EditCalendar, gets the same form read-only.
// API:
//   openForAdding(date), openForEditing(matchId)
//   saved(double matchId, string date)
//   deleteRequested(double matchId, string opponentName)
AppDialog {
    id: root

    readonly property alias viewModel: editor
    property bool newOpponentShown: false

    signal saved(double matchId, string date)
    signal deleteRequested(double matchId, string opponentName)

    function showForm() {
        // The same value as last time changes nothing in a field, so text it refused would stay.
        dateInput.showDate();
        kickoffInput.showTime();
        newOpponentInput.clear();
        editor.clearNewOpponentError();
        root.newOpponentShown = !editor.readOnly && editor.opponents.rowCount() === 0;
        root.open();
        root.focusFirst();
    }

    function focusFirst() {
        if (root.newOpponentShown)
            newOpponentInput.forceActiveFocus(Qt.TabFocusReason);
        else
            opponentInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function focusFirstError() {
        if (editor.opponentError.length > 0)
            root.focusFirst();
        else if (editor.dateError.length > 0)
            dateInput.forceActiveFocus(Qt.TabFocusReason);
        else if (editor.kickoffTimeError.length > 0)
            kickoffInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function openForAdding(date) {
        editor.startAdding(date);
        root.showForm();
    }

    function openForEditing(matchId) {
        if (editor.startEditing(matchId))
            root.showForm();
    }

    function showNewOpponent() {
        root.newOpponentShown = true;
        newOpponentInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function hideNewOpponent() {
        root.newOpponentShown = false;
        newOpponentInput.clear();
        editor.clearNewOpponentError();
        opponentInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function addNewOpponent() {
        if (editor.addOpponent(newOpponentInput.text))
            root.hideNewOpponent();
        else
            newOpponentInput.forceActiveFocus(Qt.TabFocusReason);
    }

    title: editor.adding ? qsTr("Add match") : editor.readOnly ? qsTr("Match details") : qsTr("Edit match")
    primaryText: editor.readOnly ? "" : qsTr("Save")
    secondaryText: editor.readOnly ? qsTr("Close") : qsTr("Cancel")
    dangerText: editor.adding || editor.readOnly ? "" : qsTr("Delete match")
    primaryEnabled: dateInput.validationError.length === 0 && !kickoffInput.hasInvalidText
    onPrimaryClicked: {
        if (editor.save())
            root.close();
        else
            root.focusFirstError();
    }
    onDangerClicked: root.deleteRequested(editor.matchId, editor.opponentName)

    MatchEditorViewModel {
        id: editor

        teamId: AppState.teamId
        onSaved: (matchId, date) => root.saved(matchId, date)
        onErrorOccurred: message => Toasts.error(message)
    }

    FormField {
        id: opponentField

        Layout.fillWidth: true
        label: qsTr("Opponent")
        mandatory: true
        error: editor.opponentError

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.s

            SelectField {
                id: opponentInput

                objectName: "opponentInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                textRole: "text"
                valueRole: "value"
                model: editor.opponents
                value: editor.opponentId > 0 ? editor.opponentId : null
                placeholderText: qsTr("Choose the opponent")
                Accessible.name: opponentField.label
                onValueActivated: value => editor.opponentId = value
            }

            SecondaryButton {
                objectName: "showNewOpponentButton"
                visible: !editor.readOnly && !root.newOpponentShown
                text: qsTr("Add new opponent…")
                onClicked: root.showNewOpponent()
            }
        }
    }

    FormField {
        id: newOpponentField

        Layout.fillWidth: true
        visible: root.newOpponentShown
        label: qsTr("New opponent")
        error: newOpponentInput.effectiveError

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.s

            TextInputField {
                id: newOpponentInput

                objectName: "newOpponentInput"
                Layout.fillWidth: true
                error: editor.newOpponentError
                Accessible.name: newOpponentField.label
                onTextEdited: editor.clearNewOpponentError()
                Keys.onReturnPressed: root.addNewOpponent()
                Keys.onEnterPressed: root.addNewOpponent()
                Keys.onEscapePressed: root.hideNewOpponent()
            }

            SecondaryButton {
                objectName: "addNewOpponentButton"
                text: qsTr("Add")
                onClicked: root.addNewOpponent()
            }
        }
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

                objectName: "matchDateInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                date: editor.date
                error: editor.dateError
                Accessible.name: dateField.label
                onDateEdited: date => editor.date = date
            }
        }

        FormField {
            id: kickoffField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Kick-off")
            error: kickoffInput.effectiveError

            TimeField {
                id: kickoffInput

                objectName: "kickoffInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                time: editor.kickoffTime
                error: editor.kickoffTimeError
                Accessible.name: kickoffField.label
                onTimeEdited: time => editor.kickoffTime = time
            }
        }

        FormField {
            id: venueField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Venue")
            mandatory: true

            SelectField {
                id: venueInput

                objectName: "venueInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                textRole: "text"
                valueRole: "value"
                model: editor.venueOptions
                value: editor.venue
                Accessible.name: venueField.label
                onValueActivated: value => editor.venue = value
            }
        }

        FormField {
            id: competitionField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Competition")

            SuggestionField {
                id: competitionInput

                objectName: "competitionInput"
                Layout.fillWidth: true
                enabled: !editor.readOnly
                text: editor.competition
                suggestions: editor.competitionSuggestions
                Accessible.name: competitionField.label
                onTextChanged: editor.competition = text
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: !editor.adding && editor.readOnly
        spacing: Theme.spacing.s

        Label {
            text: qsTr("Status")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
        }

        Label {
            Layout.fillWidth: true
            objectName: "matchStatusText"
            text: editor.statusText
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
        }
    }
}
