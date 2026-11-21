pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// The validation report of an import (packet P16): one row per row of the file with its status
// (badge: icon + color + word), the player, what the import does with it and why. The details
// come from the user's file, so they are plain text. The selected row's full message is shown
// below the table and exposed to accessibility, including when selection changes by keyboard.
// API:
//   model: var              rows with rowNumber, statusKey (ok/warning/error), statusText, name,
//                           action, details, message
//   emptyState: Component   passed on to DataTable
ColumnLayout {
    id: root

    property var model
    property Component emptyState
    property alias currentIndex: table.currentIndex
    readonly property alias columns: table.columns
    readonly property int count: table.count
    property string selectedMessage: ""

    spacing: Theme.spacing.s
    Accessible.role: Accessible.Grouping
    Accessible.description: selectedMessage

    function updateSelectedMessage() {
        if (!table)
            return;
        const index = table.currentIndex;
        if (!root.model || index < 0 || index >= table.count) {
            selectedMessage = "";
            return;
        }
        const row = typeof root.model.get === "function" ? root.model.get(index) : root.model[index];
        selectedMessage = row ? String(row.message ?? "") : "";
    }

    onModelChanged: updateSelectedMessage()

    Connections {
        target: root.model && typeof root.model.get === "function" ? root.model : null
        ignoreUnknownSignals: true
        function onModelReset() {
            root.updateSelectedMessage();
        }
        function onDataChanged() {
            root.updateSelectedMessage();
        }
        function onCountChanged() {
            root.updateSelectedMessage();
        }
    }

    DataTable {
        id: table

        Layout.fillWidth: true
        Layout.fillHeight: true
        model: root.model
        emptyState: root.emptyState
        Accessible.description: root.selectedMessage
        onCurrentIndexChanged: root.updateSelectedMessage()
        onCountChanged: root.updateSelectedMessage()

        columns: [
            {
                "role": "rowNumber",
                "title": qsTr("Row"),
                "width": 0.07,
                "sortable": false
            },
            {
                "role": "statusText",
                "title": qsTr("Status"),
                "width": 0.13,
                "sortable": false,
                "delegate": statusCell
            },
            {
                "role": "name",
                "title": qsTr("Player"),
                "width": 0.2,
                "sortable": false
            },
            {
                "role": "action",
                "title": qsTr("Action"),
                "width": 0.18,
                "sortable": false
            },
            {
                "role": "details",
                "title": qsTr("Details"),
                "width": 0.42,
                "sortable": false,
                "delegate": detailsCell
            }
        ]
    }

    Text {
        objectName: "importSelectedMessage"
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.selectedMessage
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.text
        font.pixelSize: Theme.fontSize.body
        Accessible.description: root.selectedMessage
    }

    Component {
        id: statusCell

        DataTableCell {
            id: cell

            StatusBadge {
                anchors.verticalCenter: parent.verticalCenter
                kind: {
                    const key = cell.rowData ? cell.rowData.statusKey : "";
                    return key === "error" ? StatusBadge.Danger : key === "warning" ? StatusBadge.Warn : StatusBadge.Ok;
                }
                text: String(cell.value ?? "")
            }
        }
    }

    Component {
        id: detailsCell

        DataTableCell {
            id: cell

            clip: true

            HoverHandler {
                id: detailsHover
            }

            Text {
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                text: String(cell.value ?? "")
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                elide: Text.ElideRight
            }

            PlainToolTip {
                visible: detailsHover.hovered && String(cell.value ?? "").length > 0
                text: cell.rowData ? cell.rowData.message : ""
                delay: Theme.tooltipDelay
            }
        }
    }
}
