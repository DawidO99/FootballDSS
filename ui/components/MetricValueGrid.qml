pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import FootballDSS

// The entry grid of a custom metric: a player in each row with one value cell. Holds nothing of its
// own: every edit goes out through a signal and comes back through the model. Not a DataTable, for
// the reasons of AttendanceGrid: every delegate stays, so Tab walks through the cells.
// Keys: Tab next cell; Enter the next row; Esc reverts the cell; on a yes/no cell Space ticks and
// Delete clears; PageUp/PageDown scroll.
// API:
//   model: var              rows with the roles of MetricEntryViewModel
//   boolean: bool           yes/no cells (CheckBoxField) instead of numbers
//   valueTitle: string      heading of the value column, e.g. "Value [s]"
//   readOnly: bool          every cell is disabled, and the grid itself takes the focus to scroll
//   count: int              read-only row count
//   focusRow(row), focusFirst()
//   valueEdited(int row, string text), checkToggled(int row, bool checked), valueCleared(int row),
//   cellEntered(int row), cellReverted(int row)
FocusScope {
    id: root

    property var model
    property bool boolean: false
    property string valueTitle: qsTr("Value")
    property bool readOnly: false
    readonly property int count: rows.count
    readonly property int numberWidth: Theme.controlHeight
    // A yes/no cell holds the box, its word and the clear button.
    readonly property int valueWidth: (root.boolean ? 6 : 4) * Theme.controlHeight

    signal valueEdited(int row, string text)
    signal checkToggled(int row, bool checked)
    signal valueCleared(int row)
    signal cellEntered(int row)
    signal cellReverted(int row)

    function focusRow(row) {
        const rowItem = rows.itemAt(row) as MetricValueGridRow;
        if (rowItem)
            rowItem.focusValue();
    }

    function focusFirst() {
        if (root.readOnly)
            root.forceActiveFocus(Qt.TabFocusReason);
        else
            root.focusRow(0);
    }

    implicitWidth: 2 * Theme.fieldWidth
    implicitHeight: Theme.tableHeaderHeight + 6 * Theme.rowHeight
    activeFocusOnTab: root.readOnly

    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageDown) {
            page.scrollByPage(1);
            event.accepted = true;
        } else if (event.key === Qt.Key_PageUp) {
            page.scrollByPage(-1);
            event.accepted = true;
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.m
        color: Theme.surface
        border.width: root.activeFocus && root.readOnly ? Theme.focusRingWidth : Theme.borderWidth
        border.color: root.activeFocus && root.readOnly ? Theme.accent : Theme.border
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.borderWidth
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.tableHeaderHeight
                color: Theme.surfaceAlt

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing.m
                    anchors.rightMargin: Theme.spacing.m
                    spacing: Theme.spacing.m

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.numberWidth
                        text: qsTr("No.")
                    }

                    AttendanceGridHeading {
                        Layout.fillWidth: true
                        text: qsTr("Player")
                    }

                    AttendanceGridHeading {
                        objectName: "metricValueHeading"
                        Layout.preferredWidth: root.valueWidth
                        horizontalAlignment: root.boolean ? Text.AlignLeft : Text.AlignHCenter
                        text: root.valueTitle
                        textFormat: Text.PlainText
                    }
                }
            }

            FocusFlickable {
                id: page

                objectName: "metricRows"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: rowsColumn.implicitHeight

                Column {
                    id: rowsColumn

                    width: page.width

                    Repeater {
                        id: rows

                        model: root.model

                        delegate: MetricValueGridRow {
                            width: rowsColumn.width
                            grid: root
                        }
                    }
                }
            }
        }
    }
}
