pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// The validation report of an import (packet P16): one row per row of the file with its status
// (badge: icon + color + word), the player, what the import does with it and why. The details
// come from the user's file, so they are plain text, with the whole text in a tooltip.
// API:
//   model: var              rows with rowNumber, statusKey (ok/warning/error), statusText, name,
//                           action, details, message
//   emptyState: Component   passed on to DataTable
DataTable {
    id: root

    columns: [
        {
            "role": "rowNumber",
            "title": qsTr("Row"),
            "width": 0.07
        },
        {
            "role": "statusText",
            "title": qsTr("Status"),
            "width": 0.13,
            "delegate": statusCell
        },
        {
            "role": "name",
            "title": qsTr("Player"),
            "width": 0.2
        },
        {
            "role": "action",
            "title": qsTr("Action"),
            "width": 0.18
        },
        {
            "role": "details",
            "title": qsTr("Details"),
            "width": 0.42,
            "delegate": detailsCell
        }
    ]

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
