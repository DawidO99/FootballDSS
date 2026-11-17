import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Starts Match Day (packet P13): asks for the half length, the default from the team category.
// The clock stays at 00:00 until the first half is started.
// API:
//   defaultHalfLength, from, to: int
//   startRequested(int halfLength)   the caller closes the dialog when the start succeeded
AppDialog {
    id: root

    property int defaultHalfLength: 45
    property int from: 10
    property int to: 60
    readonly property alias halfLengthInput: halfLengthField

    signal startRequested(int halfLength)

    size: AppDialog.Small
    title: qsTr("Start Match Day")
    primaryText: qsTr("Start")
    primaryEnabled: halfLengthField.effectiveError.length === 0
    onOpened: {
        halfLengthField.value = null;
        halfLengthField.value = root.defaultHalfLength;
        halfLengthField.forceActiveFocus();
    }
    onPrimaryClicked: root.startRequested(halfLengthField.currentValue)

    FormField {
        id: halfLengthFormField

        Layout.fillWidth: true
        label: qsTr("Half length (minutes)")
        mandatory: true
        error: halfLengthField.effectiveError

        NumberField {
            id: halfLengthField

            objectName: "halfLengthField"
            from: root.from
            to: root.to
            touch: true
            Accessible.name: halfLengthFormField.label
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("The clock starts when you start the 1st half.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.body
        wrapMode: Text.WordWrap
    }
}
