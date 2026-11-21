pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The ratings grid of the match report (packet P14): a player who played in each row, with his
// minutes, goals, assists and cards (read-only), and the rating 1–10 and RPE 0–10. Holds nothing of
// its own: every edit goes out through a signal and comes back through the model. Built like
// AttendanceGrid: a Repeater keeps every delegate, so Tab walks through the cells.
// Keys: Tab next cell; Enter the same column in the next row; Esc reverts the cell; a digit enters
// the value (the owner turns 1 then 0 into 10); Delete/Backspace clears; PageUp/PageDown scroll.
// API:
//   model: var              rows of MatchRatingsModel
//   readOnly: bool          every cell is disabled, and the grid itself takes the focus to scroll
//   count: int              read-only row count
//   focusCell(row, column), focusFirstEditable()
//   The columns are MatchRatingsModel.RatingColumn and RpeColumn.
//   digitEntered(int row, int column, int digit), cellCleared(int row, int column),
//   cellEntered(int row, int column), cellReverted(int row, int column)
FocusScope {
    id: root

    property var model
    property bool readOnly: false
    readonly property int count: rows.count
    readonly property int numberWidth: Theme.controlHeight
    readonly property int smallWidth: Theme.controlHeight + Theme.spacing.m
    readonly property int valueWidth: 2 * Theme.controlHeight

    signal digitEntered(int row, int column, int digit)
    signal cellCleared(int row, int column)
    signal cellEntered(int row, int column)
    signal cellReverted(int row, int column)

    function focusCell(row, column) {
        const rowItem = rows.itemAt(row) as MatchRatingsGridRow;
        if (rowItem)
            rowItem.focusColumn(column);
    }

    function focusFirstEditable() {
        if (root.readOnly || rows.count === 0)
            root.forceActiveFocus(Qt.TabFocusReason);
        else
            root.focusCell(0, MatchRatingsModel.RatingColumn);
    }

    function moveDown(row, column) {
        if (row + 1 < rows.count)
            root.focusCell(row + 1, column);
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
                        Layout.preferredWidth: root.smallWidth
                        text: qsTr("Pos.")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.smallWidth
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("Min.")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.smallWidth
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("Goals")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.smallWidth
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("Assists")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: 2 * root.smallWidth
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Cards")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.valueWidth
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Rating")
                    }

                    AttendanceGridHeading {
                        Layout.preferredWidth: root.valueWidth
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("RPE")
                    }
                }
            }

            FocusFlickable {
                id: page

                objectName: "ratingRows"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: rowsColumn.implicitHeight

                Column {
                    id: rowsColumn

                    width: page.width

                    Repeater {
                        id: rows

                        model: root.model

                        delegate: MatchRatingsGridRow {
                            width: rowsColumn.width
                            grid: root
                        }
                    }
                }
            }
        }
    }
}
