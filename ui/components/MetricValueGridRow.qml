import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// One row of MetricValueGrid, with the roles of MetricEntryViewModel.
Rectangle {
    id: root

    required property MetricValueGrid grid
    required property int index
    required property var shirtNumber
    required property string playerName
    required property string valueText
    required property bool checked
    required property bool hasValue
    required property string stateText
    required property string rowError
    required property bool hasError

    function focusValue() {
        if (root.grid.boolean)
            checkCell.forceActiveFocus(Qt.TabFocusReason);
        else
            valueCell.forceActiveFocus(Qt.TabFocusReason);
    }

    objectName: "metricRow_" + index
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

            MetricValueCell {
                id: valueCell

                objectName: "metricValueCell_" + root.index
                Layout.preferredWidth: root.grid.valueWidth
                visible: !root.grid.boolean
                value: root.valueText
                hasError: root.hasError
                enabled: !root.grid.readOnly
                Accessible.name: qsTr("Value of %1").arg(root.playerName)
                onValueEdited: text => root.grid.valueEdited(root.index, text)
                onReverted: root.grid.cellReverted(root.index)
                onNextRowRequested: root.grid.focusRow(root.index + 1)
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.grid.cellEntered(root.index);
                }
            }

            // Yes / no: a tick, and the word, because an empty box also means "not entered".
            RowLayout {
                Layout.preferredWidth: root.grid.valueWidth
                visible: root.grid.boolean
                spacing: Theme.spacing.xs

                CheckBoxField {
                    id: checkCell

                    objectName: "metricCheckCell_" + root.index
                    checked: root.checked
                    enabled: !root.grid.readOnly
                    Accessible.name: qsTr("%1: %2").arg(root.playerName, root.stateText)
                    onToggled: root.grid.checkToggled(root.index, checked)
                    onActiveFocusChanged: {
                        if (activeFocus)
                            root.grid.cellEntered(root.index);
                    }
                    Keys.onDeletePressed: root.grid.valueCleared(root.index)
                    Keys.onEscapePressed: root.grid.cellReverted(root.index)
                    Keys.onReturnPressed: root.grid.focusRow(root.index + 1)
                    Keys.onEnterPressed: root.grid.focusRow(root.index + 1)
                }

                Label {
                    objectName: "metricState_" + root.index
                    Layout.fillWidth: true
                    text: root.stateText
                    textFormat: Text.PlainText
                    color: root.hasValue ? Theme.text : Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                    elide: Text.ElideRight
                }
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
                objectName: "metricRowError_" + root.index
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
