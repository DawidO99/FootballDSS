import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A player on the Match Day screen (packet P13): shirt number, surname, position, minutes played,
// goals and yellow cards; a player who left the pitch says why in a word. At least Theme.hitTouch
// high for touch. While an action waits for a player, the tiles that fit it are highlighted and only
// they can be chosen.
// API:
//   shirtNumber: var        int or null
//   shortName: string       the surname (user data, plain text)
//   fullName: string        the tooltip and the accessible name
//   positionCode: string    GK, CB…
//   stateKey: string        onPitch, bench, substitutedOff, sentOff
//   stateText: string       the word for the state
//   minutesText: string     "67'" or empty
//   goals, yellowCards: int
//   choosing: bool          an action waits for a player
//   selectable: bool        this tile fits that action
//   clicked()               Enter/Return/Space click it when focused
AbstractButton {
    id: root

    property var shirtNumber: null
    property string shortName
    property string fullName
    property string positionCode
    property string stateKey: "onPitch"
    property string stateText
    property string minutesText
    property int goals: 0
    property int yellowCards: 0
    property bool choosing: false
    property bool selectable: false
    readonly property bool hasLeft: root.stateKey === "substitutedOff" || root.stateKey === "sentOff"

    implicitWidth: Theme.matchTileWidth
    implicitHeight: Math.max(Theme.matchTileHeight, Theme.hitTouch)
    enabled: !root.choosing || root.selectable
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: Theme.spacing.s
    opacity: root.choosing && !root.selectable ? Theme.disabledOpacity : 1.0
    Accessible.name: root.stateText.length > 0 ? root.fullName + ", " + root.stateText : root.fullName
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()

    background: Rectangle {
        radius: Theme.radius.m
        color: root.down ? Theme.selection : root.hovered ? Theme.rowHover : root.hasLeft ? Theme.surfaceAlt : Theme.surface
        border.width: root.visualFocus || (root.choosing && root.selectable) ? Theme.focusRingWidth : Theme.borderWidth
        border.color: root.visualFocus ? Theme.text : root.choosing && root.selectable ? Theme.accent : Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing.xs

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.s

            Label {
                objectName: "tileNumber"
                Layout.preferredWidth: Theme.shirtNumberWidth
                text: root.shirtNumber === null || root.shirtNumber === undefined ? "" : root.shirtNumber
                color: Theme.text
                font.pixelSize: Theme.fontSize.title
                font.weight: Font.DemiBold
            }

            Label {
                objectName: "tileName"
                Layout.fillWidth: true
                text: root.shortName
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Label {
                text: root.positionCode
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.xs

            Label {
                objectName: "tileMinutes"
                text: root.minutesText
                visible: root.minutesText.length > 0
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
            }

            Icon {
                visible: root.goals > 0
                name: "goal"
                size: Theme.iconSize.xs
                color: Theme.text
            }

            Label {
                visible: root.goals > 1
                text: root.goals
                color: Theme.text
                font.pixelSize: Theme.fontSize.caption
            }

            Icon {
                objectName: "tileYellow"
                visible: root.yellowCards > 0 && root.stateKey !== "sentOff"
                name: "card"
                size: Theme.iconSize.xs
                color: Theme.yellowCard
            }

            Icon {
                visible: root.stateKey === "sentOff"
                name: "card"
                size: Theme.iconSize.xs
                color: Theme.redCard
            }

            Icon {
                visible: root.stateKey === "substitutedOff"
                name: "swap"
                size: Theme.iconSize.xs
                color: Theme.textMuted
            }

            Label {
                objectName: "tileState"
                Layout.fillWidth: true
                visible: root.hasLeft
                text: root.stateText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
                elide: Text.ElideRight
            }

            Item {
                Layout.fillWidth: true
                visible: !root.hasLeft
            }
        }
    }

    PlainToolTip {
        visible: root.hovered && root.fullName.length > 0
        text: root.stateText.length > 0 ? root.fullName + " · " + root.stateText : root.fullName
        delay: Theme.tooltipDelay
    }
}
