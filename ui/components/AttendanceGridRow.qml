import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// One row of AttendanceGrid, with the roles of TrainingSessionViewModel.
Rectangle {
    id: root

    required property AttendanceGrid grid
    required property int index
    required property var shirtNumber
    required property string playerName
    required property string status
    required property var rpe
    required property var minutes
    required property var load
    required property bool cellsEnabled
    required property string rowError
    required property bool minutesHasError

    function focusColumn(column) {
        if (column === TrainingSessionViewModel.StatusColumn)
            statusCell.forceActiveFocus(Qt.TabFocusReason);
        else if (column === TrainingSessionViewModel.RpeColumn)
            rpeCell.forceActiveFocus(Qt.TabFocusReason);
        else
            minutesCell.forceActiveFocus(Qt.TabFocusReason);
    }

    objectName: "attendanceRow_" + index
    height: content.implicitHeight
    color: index % 2 === 1 ? Theme.rowAlt : Theme.surface

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.rowHeight
            Layout.leftMargin: Theme.spacing.m
            Layout.rightMargin: Theme.spacing.m
            spacing: Theme.spacing.m

            Label {
                Layout.preferredWidth: root.grid.numberWidth
                text: root.shirtNumber === undefined || root.shirtNumber === null ? "" : String(root.shirtNumber)
                textFormat: Text.PlainText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.body
            }

            Label {
                Layout.fillWidth: true
                text: root.playerName
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                elide: Text.ElideRight
            }

            // The width comes from its own content, so the column has room for the bold word.
            Item {
                Layout.preferredWidth: root.grid.statusWidth
                Layout.preferredHeight: statusCell.implicitHeight

                SegmentedControl {
                    id: statusCell

                    objectName: "statusCell_" + root.index
                    model: root.grid.statusOptions
                    currentValue: root.status
                    enabled: !root.grid.readOnly
                    Accessible.name: qsTr("Status of %1").arg(root.playerName)
                    onActivated: value => root.grid.statusPicked(root.index, String(value))
                    onActiveFocusChanged: {
                        if (activeFocus)
                            root.grid.cellEntered(root.index, TrainingSessionViewModel.StatusColumn);
                    }
                    Keys.onEscapePressed: root.grid.cellReverted(root.index, TrainingSessionViewModel.StatusColumn)
                    Keys.onReturnPressed: root.grid.moveDown(root.index, TrainingSessionViewModel.StatusColumn)
                    Keys.onEnterPressed: root.grid.moveDown(root.index, TrainingSessionViewModel.StatusColumn)
                }
            }

            RpeCell {
                id: rpeCell

                objectName: "rpeCell_" + root.index
                Layout.preferredWidth: root.grid.valueWidth
                value: root.rpe
                enabled: root.cellsEnabled
                Accessible.name: qsTr("RPE of %1").arg(root.playerName)
                onDigitEntered: digit => root.grid.digitEntered(root.index, digit)
                onCleared: root.grid.rpeCleared(root.index)
                onReverted: root.grid.cellReverted(root.index, TrainingSessionViewModel.RpeColumn)
                onNextRowRequested: root.grid.moveDown(root.index, TrainingSessionViewModel.RpeColumn)
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.grid.cellEntered(root.index, TrainingSessionViewModel.RpeColumn);
                }
            }

            MinutesCell {
                id: minutesCell

                objectName: "minutesCell_" + root.index
                Layout.preferredWidth: root.grid.valueWidth
                value: root.minutes
                hasError: root.minutesHasError
                enabled: root.cellsEnabled
                Accessible.name: qsTr("Minutes of %1").arg(root.playerName)
                onMinutesEdited: text => root.grid.minutesEdited(root.index, text)
                onReverted: root.grid.cellReverted(root.index, TrainingSessionViewModel.MinutesColumn)
                onNextRowRequested: root.grid.moveDown(root.index, TrainingSessionViewModel.MinutesColumn)
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.grid.cellEntered(root.index, TrainingSessionViewModel.MinutesColumn);
                }
            }

            Label {
                objectName: "loadCell_" + root.index
                Layout.preferredWidth: root.grid.valueWidth
                text: root.load === undefined || root.load === null ? qsTr("—") : String(root.load)
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                horizontalAlignment: Text.AlignRight
            }
        }

        // The error of the row, in the row.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing.m
            Layout.rightMargin: Theme.spacing.m
            Layout.bottomMargin: Theme.spacing.xs
            spacing: Theme.spacing.xs
            visible: root.rowError.length > 0

            Icon {
                name: "info"
                size: Theme.iconSize.s
                color: Theme.danger
            }

            Label {
                objectName: "rowError_" + root.index
                Layout.fillWidth: true
                text: root.rowError
                textFormat: Text.PlainText
                color: Theme.danger
                font.pixelSize: Theme.fontSize.caption
                wrapMode: Text.Wrap
            }
        }
    }
}
