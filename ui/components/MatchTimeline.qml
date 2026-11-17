pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The events of a match, the newest first (packet P13): minute, icon and word, players.
// API:
//   model                   rows with eventId, minuteText, typeKey, iconName, typeText,
//                           playerText, relatedText (MatchTimelineModel)
//   emptyText: string       shown without events
ListView {
    id: root

    property string emptyText: qsTr("No events yet.")

    clip: true
    spacing: Theme.spacing.xs
    boundsBehavior: Flickable.StopAtBounds
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Events")

    function iconColor(typeKey) {
        switch (typeKey) {
        case "yellow":
            return Theme.yellowCard;
        case "red":
        case "secondYellow":
            return Theme.redCard;
        case "goal":
            return Theme.ok;
        case "opponentGoal":
            return Theme.danger;
        default:
            return Theme.textMuted;
        }
    }

    ScrollBar.vertical: ScrollBar {}

    Label {
        objectName: "timelineEmpty"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        visible: root.count === 0
        text: root.emptyText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.body
        wrapMode: Text.WordWrap
    }

    delegate: Rectangle {
        id: row

        required property int index
        required property string minuteText
        required property string typeKey
        required property string iconName
        required property string typeText
        required property string playerText
        required property string relatedText

        width: ListView.view.width
        implicitHeight: rowLayout.implicitHeight + 2 * Theme.spacing.s
        radius: Theme.radius.s
        color: row.index % 2 === 0 ? Theme.surface : Theme.rowAlt

        RowLayout {
            id: rowLayout

            anchors.fill: parent
            anchors.margins: Theme.spacing.s
            spacing: Theme.spacing.s

            Label {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: minuteMetrics.advanceWidth + Theme.measuredTextSlack
                text: row.minuteText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                font.weight: Font.DemiBold
            }

            Icon {
                Layout.alignment: Qt.AlignTop
                name: row.iconName
                size: Theme.iconSize.s
                color: root.iconColor(row.typeKey)
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: row.typeText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: row.playerText.length > 0
                    text: row.playerText
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: row.relatedText.length > 0
                    text: row.relatedText
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    TextMetrics {
        id: minuteMetrics

        font.pixelSize: Theme.fontSize.body
        font.weight: Font.DemiBold
        text: "90+10'"
    }
}
