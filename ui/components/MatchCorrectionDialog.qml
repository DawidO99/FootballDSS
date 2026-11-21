import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Adds a forgotten event to a finished match (match report, packet P14): the kind, the minute as on
// the timeline ("23", "45+2"), and the players, who are those on the pitch (or the bench) at that
// minute. What the event would run into is shown before it is saved, naming a later event it
// conflicts with.
// API:
//   viewModel: MatchReportViewModel
//   openForAdding()
//   The dialog closes itself when the event was saved.
AppDialog {
    id: root

    property MatchReportViewModel viewModel
    property string eventType: "goal"
    property double firstPlayer: 0
    property double secondPlayer: 0
    readonly property alias minuteText: minuteField.text
    readonly property alias minuteInput: minuteField
    readonly property bool needsFirst: root.eventType !== "opponentGoal"
    readonly property bool hasSecond: root.eventType === "goal" || root.eventType === "sub"
    readonly property string minuteError: root.viewModel ? root.viewModel.minuteError(minuteField.text) : ""
    readonly property var firstOptions: root.viewModel && root.minuteError.length === 0 ? root.viewModel.playerOptions(root.eventType, minuteField.text, "first") : []
    readonly property var secondOptions: root.viewModel && root.minuteError.length === 0 && root.hasSecond ? root.viewModel.playerOptions(root.eventType, minuteField.text, "second") : []
    readonly property bool complete: root.minuteError.length === 0 && (!root.needsFirst || root.firstPlayer > 0) && (root.eventType !== "sub" || root.secondPlayer > 0)
    readonly property string eventError: root.viewModel && root.complete ? root.viewModel.eventError(root.eventType, minuteField.text, root.firstPlayer, root.secondPlayer) : ""

    function openForAdding() {
        root.eventType = "goal";
        root.firstPlayer = 0;
        root.secondPlayer = 0;
        minuteField.text = root.viewModel ? root.viewModel.lastMinute : "";
        typeField.showValue();
        firstField.showValue();
        secondField.showValue();
        root.open();
        typeField.forceActiveFocus(Qt.TabFocusReason);
    }

    size: AppDialog.Medium
    title: qsTr("Add event")
    primaryText: qsTr("Add event")
    primaryEnabled: root.complete && root.eventError.length === 0
    onPrimaryClicked: {
        if (!root.primaryEnabled)
            return;
        if (root.viewModel.addEvent(root.eventType, minuteField.text, root.firstPlayer, root.secondPlayer))
            root.close();
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing.m

        FormField {
            id: typeFormField

            Layout.fillWidth: true
            label: qsTr("Event")
            mandatory: true

            SelectField {
                id: typeField

                objectName: "correctionTypeField"
                model: root.viewModel ? root.viewModel.eventTypeOptions : []
                textRole: "text"
                valueRole: "value"
                value: root.eventType
                Accessible.name: typeFormField.label
                onValueActivated: value => {
                    root.eventType = value;
                    root.firstPlayer = 0;
                    root.secondPlayer = 0;
                }
            }
        }

        FormField {
            id: minuteFormField

            Layout.preferredWidth: Theme.fieldWidth / 2
            label: qsTr("Minute")
            mandatory: true
            error: root.minuteError

            TextInputField {
                id: minuteField

                objectName: "correctionMinuteField"
                implicitWidth: Theme.matchMinuteFieldWidth
                maximumLength: 7
                error: root.minuteError
                Accessible.name: minuteFormField.label
            }
        }
    }

    FormField {
        id: firstFormField

        Layout.fillWidth: true
        visible: root.needsFirst
        label: root.eventType === "sub" ? qsTr("Player coming on") : root.eventType === "goal" ? qsTr("Scorer") : qsTr("Player")
        mandatory: true

        SelectField {
            id: firstField

            objectName: "correctionFirstField"
            model: root.firstOptions
            textRole: "text"
            valueRole: "value"
            value: root.firstPlayer
            placeholderText: qsTr("Choose a player")
            Accessible.name: firstFormField.label
            onValueActivated: value => root.firstPlayer = value
        }
    }

    FormField {
        id: secondFormField

        Layout.fillWidth: true
        visible: root.hasSecond
        label: root.eventType === "sub" ? qsTr("Player going off") : qsTr("Assist (optional)")
        mandatory: root.eventType === "sub"

        SelectField {
            id: secondField

            objectName: "correctionSecondField"
            model: root.secondOptions
            textRole: "text"
            valueRole: "value"
            value: root.secondPlayer
            placeholderText: qsTr("Choose a player")
            Accessible.name: secondFormField.label
            onValueActivated: value => root.secondPlayer = value
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.eventError.length > 0
        spacing: Theme.spacing.xs

        Icon {
            Layout.alignment: Qt.AlignTop
            name: "warn"
            size: Theme.iconSize.s
            color: Theme.danger
        }

        Label {
            objectName: "correctionError"
            Layout.fillWidth: true
            text: root.eventError
            textFormat: Text.PlainText
            color: Theme.danger
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("The minutes of the players and the score are counted again from the events.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.caption
        wrapMode: Text.Wrap
    }
}
