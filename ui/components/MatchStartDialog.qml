import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Starts Match Day (packet P13): the format of the match, a preset or 1–4 parts of 10–60 minutes
// (at most 130 in all; the default from the team category), and the clock: with the clock (the
// default; it stays at 00:00 until the first part is started) or without it, every minute typed
// (the default for a match before today, entered after it). The fields stand one under the other,
// so they fit with their labels at the minimum window in both languages. The sum is checked once the
// values are finished: when neither field has the focus any more, or at Start. Enter starts with what
// is shown.
// API:
//   formatOptions: var       [{ value: "2x45", text }…, { value: "custom", text }]
//   defaultFormat: string    the preset chosen when the dialog opens
//   defaultClockless: bool   the mode chosen when the dialog opens
//   minPeriods, maxPeriods, minPeriodLength, maxPeriodLength, maxMinutes: int
//   startRequested(string format, int periods, int periodLength, bool clockless)
//                            the caller closes the dialog when the start succeeded
AppDialog {
    id: root

    property var formatOptions: []
    property string defaultFormat: "2x45"
    property bool defaultClockless: false
    property int minPeriods: 1
    property int maxPeriods: 4
    property int minPeriodLength: 10
    property int maxPeriodLength: 60
    property int maxMinutes: 130
    property string chosenFormat: root.defaultFormat
    property bool clockless: root.defaultClockless
    // The sum is shown wrong only for finished values (P13 manual test).
    property bool sumChecked: false
    readonly property bool custom: root.chosenFormat === "custom"
    readonly property alias formatInput: formatField
    readonly property alias periodsInput: periodsField
    readonly property alias periodLengthInput: lengthField
    readonly property alias periodsFormInput: periodsFormField
    readonly property alias periodLengthFormInput: lengthFormField
    readonly property alias clockInput: clockField
    readonly property bool customTooLong: root.custom && periodsField.currentValue !== null && lengthField.currentValue !== null && periodsField.currentValue * lengthField.currentValue > root.maxMinutes
    // Not while either value is being typed: only when both are finished.
    readonly property bool showSumError: root.sumChecked && root.customTooLong && !periodsField.activeFocus && !lengthField.activeFocus

    signal startRequested(string format, int periods, int periodLength, bool clockless)

    size: AppDialog.Small
    touch: true
    title: qsTr("Start Match Day")
    primaryText: qsTr("Start")
    primaryEnabled: !root.custom || (periodsField.effectiveError.length === 0 && lengthField.effectiveError.length === 0)
    onOpened: {
        root.chosenFormat = root.defaultFormat;
        root.clockless = root.defaultClockless;
        root.sumChecked = false;
        formatField.showValue();
        periodsField.value = null;
        periodsField.value = 2;
        lengthField.value = null;
        lengthField.value = 45;
    }
    onPrimaryClicked: {
        if (root.customTooLong) {
            root.sumChecked = true;
            return;
        }
        root.startRequested(root.chosenFormat, periodsField.currentValue ?? 0, lengthField.currentValue ?? 0, root.clockless);
    }

    FormField {
        id: formatFormField

        Layout.fillWidth: true
        label: qsTr("Match format")

        SelectField {
            id: formatField

            objectName: "formatField"
            Layout.fillWidth: true
            touch: true
            model: root.formatOptions
            textRole: "text"
            valueRole: "value"
            value: root.chosenFormat
            Accessible.name: formatFormField.label
            onValueActivated: value => {
                root.chosenFormat = value;
                root.sumChecked = false;
            }
        }
    }

    FormField {
        id: periodsFormField

        objectName: "periodsFormField"
        Layout.fillWidth: true
        visible: root.custom
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
            onActiveFocusChanged: {
                if (!activeFocus)
                    root.sumChecked = true;
            }
        }
    }

    FormField {
        id: lengthFormField

        objectName: "periodLengthFormField"
        Layout.fillWidth: true
        visible: root.custom
        label: qsTr("Minutes per part")
        mandatory: true
        error: lengthField.effectiveError.length > 0 ? lengthField.effectiveError : root.showSumError ? qsTr("At most %1 minutes in all.").arg(root.maxMinutes) : ""

        NumberField {
            id: lengthField

            objectName: "periodLengthField"
            from: root.minPeriodLength
            to: root.maxPeriodLength
            touch: true
            Accessible.name: lengthFormField.label
            onActiveFocusChanged: {
                if (!activeFocus)
                    root.sumChecked = true;
            }
        }
    }

    FormField {
        id: clockFormField

        Layout.fillWidth: true
        label: qsTr("Clock")
        helpText: root.clockless ? qsTr("No clock: you type the minute of every action, for example to enter a match after it was played.") : qsTr("The clock starts when you start the first part.")

        SegmentedControl {
            id: clockField

            objectName: "clockModeField"
            Layout.fillWidth: true
            touch: true
            model: [
                {
                    "value": false,
                    "text": qsTr("With the clock")
                },
                {
                    "value": true,
                    "text": qsTr("Without the clock")
                }
            ]
            currentValue: root.clockless
            Accessible.name: clockFormField.label
            onActivated: value => root.clockless = value
        }
    }
}
