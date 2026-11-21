pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// The column mapping of an import (packet P16): per column of the file its header, a few sample
// values and what it means, chosen in a SelectField ("Ignore column" included). A column matched
// automatically says so with a badge. Not scrollable itself: the page around it scrolls, and Tab
// goes through the choices from top to bottom.
// API:
//   model: var              rows with column, header, samples, field (key, "" = ignored), autoMatched
//   fieldOptions: var       [{ value, text }] for the choice
//   fieldChosen(int column, string field)
// Each choice has objectName "mappingChoice<column>".
ColumnLayout {
    id: root

    property var model
    property var fieldOptions: []
    readonly property int count: rows.count

    signal fieldChosen(int column, string field)

    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.tableHeaderHeight
        spacing: Theme.spacing.m

        Text {
            Layout.preferredWidth: Theme.importHeaderWidth
            text: qsTr("Column in the file")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
            font.bold: true
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Examples")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
            font.bold: true
            elide: Text.ElideRight
        }

        Text {
            Layout.preferredWidth: Theme.importFieldWidth
            text: qsTr("Meaning")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
            font.bold: true
            elide: Text.ElideRight
        }
    }

    Repeater {
        id: rows

        model: root.model

        delegate: Rectangle {
            id: row

            required property int index
            required property int column
            required property string header
            required property string samples
            required property string field
            required property bool autoMatched

            Layout.fillWidth: true
            implicitHeight: Math.max(Theme.rowHeight, rowLayout.implicitHeight + 2 * Theme.spacing.xs)
            color: index % 2 === 1 ? Theme.rowAlt : "transparent"

            RowLayout {
                id: rowLayout

                anchors.fill: parent
                anchors.topMargin: Theme.spacing.xs
                anchors.bottomMargin: Theme.spacing.xs
                spacing: Theme.spacing.m

                Text {
                    Layout.preferredWidth: Theme.importHeaderWidth
                    text: row.header
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: row.samples
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    elide: Text.ElideRight
                }

                StatusBadge {
                    visible: row.autoMatched
                    kind: StatusBadge.Ok
                    text: qsTr("Recognised")
                }

                SelectField {
                    id: choice

                    objectName: "mappingChoice" + row.column
                    Layout.preferredWidth: Theme.importFieldWidth
                    model: root.fieldOptions
                    textRole: "text"
                    valueRole: "value"
                    value: row.field
                    Accessible.name: qsTr("Meaning of the column %1").arg(row.header)
                    onValueActivated: chosen => root.fieldChosen(row.column, chosen)
                }
            }
        }
    }
}
