import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Starts Match Day (packet P13): the format of the match, a preset or 1–4 parts of 10–60 minutes
// (at most 130 in all); the default from the team category. The clock stays at 00:00 until the
// first part is started. Enter starts with the preset shown; Tab reaches the fields.
// API:
//   formatOptions: var       [{ value: "2x45", text }…, { value: "custom", text }]
//   defaultFormat: string    the preset chosen when the dialog opens
//   minPeriods, maxPeriods, minPeriodLength, maxPeriodLength, maxMinutes: int
//   startRequested(string format, int periods, int periodLength)   the caller closes the dialog
//                                                                 when the start succeeded
AppDialog {
    id: root

    property var formatOptions: []
    property string defaultFormat: "2x45"
    property int minPeriods: 1
    property int maxPeriods: 4
    property int minPeriodLength: 10
    property int maxPeriodLength: 60
    property int maxMinutes: 130
    property string chosenFormat: root.defaultFormat
    readonly property bool custom: root.chosenFormat === "custom"
    readonly property alias formatInput: formatField
    readonly property alias periodsInput: periodsField
    readonly property alias periodLengthInput: lengthField
    readonly property bool customTooLong: root.custom && periodsField.currentValue !== null && lengthField.currentValue !== null && periodsField.currentValue * lengthField.currentValue > root.maxMinutes

    signal startRequested(string format, int periods, int periodLength)

    size: AppDialog.Small
    touch: true
    title: qsTr("Start Match Day")
    primaryText: qsTr("Start")
    primaryEnabled: !root.custom || (periodsField.effectiveError.length === 0 && lengthField.effectiveError.length === 0 && !root.customTooLong)
    onOpened: {
        root.chosenFormat = root.defaultFormat;
        formatField.showValue();
        periodsField.value = null;
        periodsField.value = 2;
        lengthField.value = null;
        lengthField.value = 45;
    }
    onPrimaryClicked: root.startRequested(root.chosenFormat, periodsField.currentValue ?? 0, lengthField.currentValue ?? 0)

    FormField {
        id: formatFormField

        Layout.fillWidth: true
        label: qsTr("Match format")

        SelectField {
            id: formatField

            objectName: "formatField"
            touch: true
            model: root.formatOptions
            textRole: "text"
            valueRole: "value"
            value: root.chosenFormat
            Accessible.name: formatFormField.label
            onValueActivated: value => root.chosenFormat = value
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.custom
        spacing: Theme.spacing.m

        FormField {
            id: periodsFormField

            Layout.fillWidth: true
            label: qsTr("Parts")
            mandatory: true
            error: periodsField.effectiveError

            NumberField {
                id: periodsField

                objectName: "periodsField"
                from: root.minPeriods
                to: root.maxPeriods
                touch: true
                Accessible.name: periodsFormField.label
            }
        }

        FormField {
            id: lengthFormField

            Layout.fillWidth: true
            label: qsTr("Minutes per part")
            mandatory: true
            error: lengthField.effectiveError

            NumberField {
                id: lengthField

                objectName: "periodLengthField"
                from: root.minPeriodLength
                to: root.maxPeriodLength
                touch: true
                Accessible.name: lengthFormField.label
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.customTooLong
        spacing: Theme.spacing.s

        Icon {
            name: "warn"
            size: Theme.iconSize.s
            color: Theme.danger
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("At most %1 minutes in all.").arg(root.maxMinutes)
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.WordWrap
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("The clock starts when you start the first part.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.body
        wrapMode: Text.WordWrap
    }
}
