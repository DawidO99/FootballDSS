import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Label + mandatory marker + input + help text + error text. The input goes into the default slot.
// API:
//   label: string, mandatory: bool, helpText: string
//   error: string    empty = valid; bind it to the input's effectiveError and/or a viewmodel message
//   hasError         read-only
// Example:
//   FormField {
//       id: birthField
//       label: qsTr("Birth date")
//       mandatory: true
//       error: birthDate.effectiveError
//       DateField { id: birthDate; Accessible.name: birthField.label }
//   }
// The error line shows an icon and the message, so the state is never color-only.
ColumnLayout {
    id: root

    property string label
    property bool mandatory: false
    property string helpText
    property string error
    readonly property bool hasError: error.length > 0

    default property alias content: slot.data

    spacing: Theme.spacing.xs

    Row {
        visible: root.label.length > 0
        spacing: Theme.spacing.xs

        Label {
            text: root.label
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            font.weight: Font.DemiBold
        }

        Label {
            visible: root.mandatory
            //: Marker after the label of a mandatory form field.
            text: qsTr("*")
            color: Theme.danger
            font.pixelSize: Theme.fontSize.body
            font.weight: Font.DemiBold
            Accessible.name: qsTr("required")
        }
    }

    ColumnLayout {
        id: slot

        Layout.fillWidth: true
        spacing: 0
    }

    // Messages wrap inside the width of the field instead of widening it.
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        visible: root.hasError
        spacing: Theme.spacing.xs

        Icon {
            Layout.alignment: Qt.AlignTop
            name: "danger"
            size: Theme.iconSize.s
            color: Theme.danger
        }

        Label {
            Layout.fillWidth: true
            text: root.error
            color: Theme.danger
            font.pixelSize: Theme.fontSize.caption
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        visible: !root.hasError && root.helpText.length > 0
        text: root.helpText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.caption
        wrapMode: Text.WordWrap
    }
}
