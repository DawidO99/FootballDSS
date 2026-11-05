pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The training grid: a player in each row, with status, RPE, minutes and load. Holds nothing of its
// own: every edit goes out through a signal and comes back through the model.
// Not a DataTable: a ListView destroys what scrolls away, with its focus and its text, and Enter there
// opens a row. A squad is a few dozen rows, so a Repeater keeps every delegate, and Tab simply walks
// through the cells, past the disabled ones.
// Keys: Tab next cell; Enter the same column in the nearest row where it is enabled; Esc reverts the
// cell; Left/Right the status; a digit the RPE; PageUp/PageDown scroll.
// API:
//   model: var              rows with the roles of TrainingSessionViewModel
//   statusOptions: var      [{ value, text }]
//   readOnly: bool          every cell is disabled, and the grid itself takes the focus to scroll
//   count: int              read-only row count
//   focusCell(row, column), focusFirstEditable()
//   The columns are TrainingSessionViewModel.StatusColumn, RpeColumn and MinutesColumn.
//   statusPicked(int row, string status), digitEntered(int row, int digit), rpeCleared(int row),
//   minutesEdited(int row, string text), cellEntered(int row, int column),
//   cellReverted(int row, int column)
FocusScope {
    id: root

    property var model
    property var statusOptions: []
    property bool readOnly: false
    readonly property int count: rows.count
    readonly property int numberWidth: Theme.controlHeight
    readonly property int valueWidth: 2 * Theme.controlHeight
    // The word of the selected status is in bold, and a little wider.
    readonly property int statusWidth: statusMetrics.implicitWidth + Theme.spacing.m

    signal statusPicked(int row, string status)
    signal digitEntered(int row, int digit)
    signal rpeCleared(int row)
    signal minutesEdited(int row, string text)
    signal cellEntered(int row, int column)
    signal cellReverted(int row, int column)

    function focusCell(row, column) {
        const rowItem = rows.itemAt(row) as AttendanceGridRow;
        if (rowItem)
            rowItem.focusColumn(column);
    }

    function focusFirstEditable() {
        for (let row = 0; row < rows.count; ++row) {
            const rowItem = rows.itemAt(row) as AttendanceGridRow;
            if (rowItem && rowItem.cellsEnabled) {
                rowItem.focusColumn(TrainingSessionViewModel.RpeColumn);
                return;
            }
        }
        if (root.readOnly)
            root.forceActiveFocus(Qt.TabFocusReason);
        else
            root.focusCell(0, TrainingSessionViewModel.StatusColumn);
    }

    function moveDown(row, column) {
        for (let next = row + 1; next < rows.count; ++next) {
            const rowItem = rows.itemAt(next) as AttendanceGridRow;
            if (rowItem && (column === TrainingSessionViewModel.StatusColumn || rowItem.cellsEnabled)) {
                rowItem.focusColumn(column);
                return;
            }
        }
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

    // Measures the widest status, in the language of the app. Never shown, and it takes no focus.
    Item {
        visible: false

        SegmentedControl {
            id: statusMetrics

            enabled: false
            model: root.statusOptions
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
                        Layout.preferredWidth: root.statusWidth
                        text: qsTr("Status")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.valueWidth
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("RPE")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.valueWidth
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Minutes")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.valueWidth
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("Load")
                    }
                }
            }

            FocusFlickable {
                id: page

                objectName: "attendanceRows"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: rowsColumn.implicitHeight

                Column {
                    id: rowsColumn

                    width: page.width

                    Repeater {
                        id: rows

                        model: root.model

                        delegate: AttendanceGridRow {
                            width: rowsColumn.width
                            grid: root
                        }
                    }
                }
            }
        }
    }
}
