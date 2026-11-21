import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// One row of MatchRatingsGrid, with the roles of MatchRatingsModel.
Rectangle {
    id: root

    required property MatchRatingsGrid grid
    required property int index
    required property var shirtNumber
    required property string playerName
    required property string positionCode
    required property string minutesText
    required property int goals
    required property int assists
    required property int yellowCards
    required property int redCards
    required property var rating
    required property var rpe
    required property string rowError
    required property bool changed

    function focusColumn(column) {
        if (column === MatchRatingsModel.RpeColumn)
            rpeCell.forceActiveFocus(Qt.TabFocusReason);
        else
            ratingCell.forceActiveFocus(Qt.TabFocusReason);
    }

    objectName: "ratingRow_" + index
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
                font.weight: root.changed ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }

            Label {
                Layout.preferredWidth: root.grid.smallWidth
                text: root.positionCode
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.body
            }

            Label {
                objectName: "minutesText_" + root.index
                Layout.preferredWidth: root.grid.smallWidth
                text: root.minutesText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                horizontalAlignment: Text.AlignRight
            }

            Label {
                Layout.preferredWidth: root.grid.smallWidth
                text: root.goals > 0 ? String(root.goals) : ""
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                horizontalAlignment: Text.AlignRight
            }

            Label {
                Layout.preferredWidth: root.grid.smallWidth
                text: root.assists > 0 ? String(root.assists) : ""
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                horizontalAlignment: Text.AlignRight
            }

            // Color + icon + number; the word is in the accessible name and the tooltip.
            Row {
                Layout.preferredWidth: 2 * root.grid.smallWidth
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.spacing.xs

                Repeater {
                    model: [
                        {
                            "count": root.yellowCards,
                            "color": Theme.yellowCard,
                            "word": qsTr("Yellow cards: %1").arg(root.yellowCards)
                        },
                        {
                            "count": root.redCards,
                            "color": Theme.redCard,
                            "word": qsTr("Red cards: %1").arg(root.redCards)
                        }
                    ]

                    delegate: Row {
                        id: cardItem

                        required property var modelData

                        visible: cardItem.modelData.count > 0
                        spacing: Theme.spacing.xs
                        Accessible.role: Accessible.StaticText
                        Accessible.name: cardItem.modelData.word

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "card"
                            size: Theme.iconSize.s
                            color: cardItem.modelData.color
                        }

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: String(cardItem.modelData.count)
                            color: Theme.text
                            font.pixelSize: Theme.fontSize.body
                        }

                        HoverHandler {
                            id: cardHover
                        }

                        ToolTip.visible: cardHover.hovered
                        ToolTip.text: cardItem.modelData.word
                        ToolTip.delay: Theme.tooltipDelay
                    }
                }
            }

            RpeCell {
                id: ratingCell

                objectName: "ratingCell_" + root.index
                Layout.preferredWidth: root.grid.valueWidth
                value: root.rating
                enabled: !root.grid.readOnly
                Accessible.name: qsTr("Rating of %1").arg(root.playerName)
                onDigitEntered: digit => root.grid.digitEntered(root.index, MatchRatingsModel.RatingColumn, digit)
                onCleared: root.grid.cellCleared(root.index, MatchRatingsModel.RatingColumn)
                onReverted: root.grid.cellReverted(root.index, MatchRatingsModel.RatingColumn)
                onNextRowRequested: root.grid.moveDown(root.index, MatchRatingsModel.RatingColumn)
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.grid.cellEntered(root.index, MatchRatingsModel.RatingColumn);
                }
            }

            RpeCell {
                id: rpeCell

                objectName: "matchRpeCell_" + root.index
                Layout.preferredWidth: root.grid.valueWidth
                value: root.rpe
                enabled: !root.grid.readOnly
                Accessible.name: qsTr("RPE of %1").arg(root.playerName)
                onDigitEntered: digit => root.grid.digitEntered(root.index, MatchRatingsModel.RpeColumn, digit)
                onCleared: root.grid.cellCleared(root.index, MatchRatingsModel.RpeColumn)
                onReverted: root.grid.cellReverted(root.index, MatchRatingsModel.RpeColumn)
                onNextRowRequested: root.grid.moveDown(root.index, MatchRatingsModel.RpeColumn)
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.grid.cellEntered(root.index, MatchRatingsModel.RpeColumn);
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
                objectName: "ratingRowError_" + root.index
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
