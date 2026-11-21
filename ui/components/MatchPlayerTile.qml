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
//   goals, yellowCards: int  a yellow card shows its icon and the words "Yellow card"
//   choosing: bool          an action waits for a player
//   selectable: bool        this tile fits that action
//   historyText: string     where the player was at the minute of the action when that differs
//                           from now ("At 11': on the pitch"); shown instead of the state
//   playerId: var           for a drag
//   dragEnabled: bool       Match Day tactics (P13b): the tile can be dragged on a token of the
//                           board (Drag.keys "fdss-player", source.payload { kind: "bench",
//                           playerId }); a copy follows the pointer in the overlay
//   dragging: bool          read-only: the tile is being dragged
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
    property string historyText
    property var playerId: 0
    property bool dragEnabled: false
    readonly property bool dragging: tileDrag.active
    readonly property bool hasLeft: root.stateKey === "substitutedOff" || root.stateKey === "sentOff"
    // A yellow card is an icon and a word, never the colour alone (AGENTS section 6).
    readonly property bool showsYellow: root.yellowCards > 0 && root.stateKey !== "sentOff"
    readonly property string yellowCardText: qsTr("Yellow card")

    implicitWidth: Theme.matchTileWidth
    implicitHeight: Math.max(Theme.matchTileHeight, Theme.hitTouch)
    enabled: !root.choosing || root.selectable
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: Theme.spacing.s
    opacity: root.choosing && !root.selectable ? Theme.disabledOpacity : 1.0
    Accessible.name: [root.fullName, root.historyText.length > 0 ? root.historyText : root.stateText, root.showsYellow ? root.yellowCardText : ""].filter(part => part.length > 0).join(", ")
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
                visible: root.showsYellow
                name: "card"
                size: Theme.iconSize.xs
                color: Theme.yellowCard
            }

            Label {
                objectName: "tileYellowText"
                Layout.fillWidth: true
                visible: root.showsYellow && root.historyText.length === 0
                text: root.yellowCardText
                color: Theme.text
                font.pixelSize: Theme.fontSize.caption
                elide: Text.ElideRight
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
                objectName: "tileHistory"
                Layout.fillWidth: true
                visible: root.historyText.length > 0
                text: root.historyText
                color: Theme.text
                font.pixelSize: Theme.fontSize.caption
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Label {
                objectName: "tileState"
                Layout.fillWidth: true
                visible: root.hasLeft && root.historyText.length === 0
                text: root.stateText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
                elide: Text.ElideRight
            }

            Item {
                Layout.fillWidth: true
                visible: !root.hasLeft && !root.showsYellow && root.historyText.length === 0
            }
        }
    }

    function moveGhost(scenePosition) {
        if (!ghost.visible || !ghost.parent)
            return;
        const point = ghost.parent.mapFromItem(null, scenePosition.x, scenePosition.y);
        ghost.x = point.x - ghost.Drag.hotSpot.x;
        ghost.y = point.y - ghost.Drag.hotSpot.y;
    }

    DragHandler {
        id: tileDrag

        target: null
        enabled: root.dragEnabled && root.enabled
        dragThreshold: Theme.pitch.dragThreshold
        onActiveChanged: {
            if (active) {
                ghost.payload = {
                    "kind": "bench",
                    "playerId": root.playerId
                };
                ghost.visible = true;
                root.moveGhost(centroid.scenePosition);
            } else if (ghost.visible) {
                ghost.Drag.drop();
                ghost.visible = false;
            }
        }
        onCentroidChanged: root.moveGhost(centroid.scenePosition)
    }

    // The copy that follows the pointer; in the overlay, so no view clips it.
    PlayerToken {
        id: ghost

        objectName: "tileDragGhost"
        parent: Overlay.overlay
        ghost: true
        visible: false
        z: 1
        width: implicitWidth
        height: implicitHeight
        positionText: root.positionCode
        shirtNumber: root.shirtNumber
        shortName: root.shortName
        fullName: root.fullName
        Drag.active: visible
        Drag.keys: ["fdss-player"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: Theme.pitch.tokenSize / 2
    }

    PlainToolTip {
        visible: root.hovered && !root.dragging && root.fullName.length > 0
        text: [root.fullName, root.stateText, root.showsYellow ? root.yellowCardText : ""].filter(part => part.length > 0).join(" · ")
        delay: Theme.tooltipDelay
    }
}
