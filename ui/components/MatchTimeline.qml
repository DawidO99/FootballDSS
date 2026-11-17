pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The events and notes of a match, the latest in the match first (packet P13): minute, icon and
// word, players or the text of a note. A note can be edited or deleted from its row.
// API:
//   model                   rows of MatchTimelineModel (kind, eventId, noteId, minuteText, typeKey,
//                           iconName, typeText, playerText, relatedText, noteMinute,
//                           notePlayerId, noteText)
//   emptyText: string       shown without events
//   notesEditable: bool     the buttons of the notes are shown
//   noteEditRequested(double noteId, string minute, double playerId, string text)
//   noteDeleteRequested(double noteId)
ListView {
    id: root

    property string emptyText: qsTr("No events yet.")
    property bool notesEditable: false

    signal noteEditRequested(double noteId, string minute, double playerId, string text)
    signal noteDeleteRequested(double noteId)

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
        case "note":
            return Theme.info;
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
        required property string kind
        required property var noteId
        required property string noteMinute
        required property var notePlayerId
        required property string noteText
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
                    color: row.kind === "note" ? Theme.text : Theme.textMuted
                    font.pixelSize: row.kind === "note" ? Theme.fontSize.body : Theme.fontSize.caption
                    wrapMode: Text.WordWrap
                }
            }

            IconButton {
                objectName: "editNoteButton"
                Layout.alignment: Qt.AlignTop
                visible: row.kind === "note" && root.notesEditable
                touch: true
                iconName: "edit"
                text: qsTr("Edit note")
                onClicked: root.noteEditRequested(row.noteId, row.noteMinute, row.notePlayerId, row.noteText)
            }

            IconButton {
                objectName: "deleteNoteButton"
                Layout.alignment: Qt.AlignTop
                visible: row.kind === "note" && root.notesEditable
                touch: true
                iconName: "trash"
                iconColor: Theme.danger
                text: qsTr("Delete note")
                onClicked: root.noteDeleteRequested(row.noteId)
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
