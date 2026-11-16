import QtQuick
import QtQuick.Controls.Basic

// The load status of a player (SPEC section 7.1): a signal to talk to the player, never a
// diagnosis. A StatusBadge (color + icon + word) and, for a row of the team table that describes
// another day than the table, that day beside it as text.
// API:
//   statusKey: string        high, elevated, normal, underloaded or notEnoughData
//   text: string             the word on the badge, from the viewmodel
//   note: string             a day and month shown beside the badge; empty: none
//   accessibleName: string   the full status (and its day) when the badge shows a short word;
//                            defaults to text
Row {
    id: root

    property string statusKey
    property string text
    property string note
    property string accessibleName: text

    spacing: Theme.spacing.s

    Accessible.role: Accessible.StaticText
    Accessible.name: root.accessibleName

    StatusBadge {
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        Accessible.ignored: true
        kind: {
            switch (root.statusKey) {
            case "high":
                return StatusBadge.Danger;
            case "elevated":
                return StatusBadge.Warn;
            case "normal":
                return StatusBadge.Ok;
            case "underloaded":
                return StatusBadge.Info;
            default:
                return StatusBadge.Neutral;
            }
        }
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        visible: root.note.length > 0
        spacing: Theme.spacing.xs

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "calendar"
            size: Theme.iconSize.xs
            color: Theme.textMuted
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: root.note
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
        }
    }
}
