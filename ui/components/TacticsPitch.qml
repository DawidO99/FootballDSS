pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// The tactics board (P12): a pitch drawn in QML with the own goal at the bottom (SPEC
// `.tactic.json`: y = 1), the player tokens on their slots and the position zones (P12 decision 12),
// drawn while a token is dragged. Mouse, touch and keyboard: a token is a button (Tab, Enter), and
// a drag starts after Theme.pitch.dragThreshold. During a drag a copy of the token follows the
// pointer in the overlay; dropping it on another token swaps the players, on the pitch moves the
// slot to the zone there; a free slot moves the same way. Players dragged from the bench or the
// squad list (Drag.keys "fdss-player", source.payload { kind, playerId }) are dropped on a token.
// API:
//   model: TacticsBoardModel     slotIndex, positionText, pitchX, pitchY, playerId, shirtNumber,
//                                shortName, fullName, severity, issueText, selected
//   zones: var                   [{ position, text, x, y, width, height }], 0–1
//   interactive: bool            drags and drops; a read-only board still selects
//   dragging: bool               read-only: a token is being dragged
//   slotActivated(slot, pointer) a click or tap (pointer true; also the first of a double one),
//                                Enter or Space (pointer false) on a token
//   slotDoubleTapped(slot)       the second click or tap of a double one
//   playerDropped(slot, playerId)   a player from the bench or the list dropped on a token
//   slotsSwapped(from, to)       a token dropped on another token
//   slotMoved(slot, x, y)        a token dropped on the pitch, x and y 0–1
//   removeRequested(slot)        Delete on a token
//   benchRequested(slot)         B on a token
//   focusSlot(slot)              gives a token the keyboard focus
//   tokenAt(slot): Item          the token of a slot, for the tests
Item {
    id: root

    property var model
    property var zones: []
    property bool interactive: true
    readonly property bool dragging: ghost.visible
    // The zone under a dragged token, -1: none.
    readonly property int hoverZone: pitchDrop.containsDrag ? root.zoneAt(pitchDrop.drag.x / pitch.width, pitchDrop.drag.y / pitch.height) : -1
    readonly property alias pitchItem: pitch

    signal slotActivated(int slot, bool pointer)
    signal slotDoubleTapped(int slot)
    signal playerDropped(int slot, double playerId)
    signal slotsSwapped(int from, int to)
    signal slotMoved(int slot, real x, real y)
    signal removeRequested(int slot)
    signal benchRequested(int slot)

    function focusSlot(slot) {
        const token = root.tokenAt(slot);
        if (token)
            token.forceActiveFocus(Qt.TabFocusReason);
    }

    function tokenAt(slot) {
        const item = slots.itemAt(slot);
        return item ? item.token : null;
    }

    function zoneAt(x, y) {
        for (let i = 0; i < root.zones.length; ++i) {
            const zone = root.zones[i];
            if (x >= zone.x && x <= zone.x + zone.width && y >= zone.y && y <= zone.y + zone.height)
                return i;
        }
        return -1;
    }

    function startDrag(token, payload) {
        ghost.payload = payload;
        ghost.positionText = token.positionText;
        ghost.shirtNumber = token.shirtNumber;
        ghost.shortName = token.shortName;
        ghost.fullName = token.fullName;
        ghost.visible = true;
        root.moveGhost(token.dragScenePosition);
    }

    function moveGhost(scenePosition) {
        if (!ghost.visible || !ghost.parent)
            return;
        const point = ghost.parent.mapFromItem(null, scenePosition.x, scenePosition.y);
        ghost.x = point.x - ghost.Drag.hotSpot.x;
        ghost.y = point.y - ghost.Drag.hotSpot.y;
    }

    function finishDrag() {
        if (!ghost.visible)
            return;
        ghost.Drag.drop();
        ghost.visible = false;
    }

    implicitWidth: Theme.pitch.preferredWidth
    implicitHeight: implicitWidth / Theme.pitch.aspect

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Tactics board")

    Rectangle {
        id: pitch

        anchors.centerIn: parent
        height: Math.min(root.height, root.width / Theme.pitch.aspect)
        width: height * Theme.pitch.aspect
        color: Theme.pitch.grass
        radius: Theme.radius.s
        clip: false

        // Mowing stripes.
        Column {
            anchors.fill: parent

            Repeater {
                model: Theme.pitch.stripes

                Rectangle {
                    required property int index

                    width: pitch.width
                    height: pitch.height / Theme.pitch.stripes
                    color: index % 2 === 0 ? Theme.pitch.grass : Theme.pitch.grassStripe
                }
            }
        }

        // Markings, in proportions of a 68 × 105 m pitch.
        Item {
            id: markings

            anchors.fill: parent
            anchors.margins: Theme.spacing.s

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: Theme.pitch.lineWidth
                border.color: Theme.pitch.line
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: Theme.pitch.lineWidth
                color: Theme.pitch.line
            }

            Rectangle {
                anchors.centerIn: parent
                width: parent.width * Theme.pitch.centreCircleMetres / Theme.pitch.widthMetres
                height: width
                radius: width / 2
                color: "transparent"
                border.width: Theme.pitch.lineWidth
                border.color: Theme.pitch.line
            }

            Repeater {
                // The penalty and goal areas at the top (the opponent) and at the bottom (own goal).
                model: [
                    {
                        "top": true,
                        "width": Theme.pitch.penaltyAreaWidthMetres,
                        "depth": Theme.pitch.penaltyAreaDepthMetres
                    },
                    {
                        "top": true,
                        "width": Theme.pitch.goalAreaWidthMetres,
                        "depth": Theme.pitch.goalAreaDepthMetres
                    },
                    {
                        "top": false,
                        "width": Theme.pitch.penaltyAreaWidthMetres,
                        "depth": Theme.pitch.penaltyAreaDepthMetres
                    },
                    {
                        "top": false,
                        "width": Theme.pitch.goalAreaWidthMetres,
                        "depth": Theme.pitch.goalAreaDepthMetres
                    }
                ]

                Rectangle {
                    required property var modelData

                    anchors.horizontalCenter: parent.horizontalCenter
                    y: modelData.top ? 0 : markings.height - height
                    width: markings.width * modelData.width / Theme.pitch.widthMetres
                    height: markings.height * modelData.depth / Theme.pitch.lengthMetres
                    color: "transparent"
                    border.width: Theme.pitch.lineWidth
                    border.color: Theme.pitch.line
                }
            }
        }

        // Position zones, shown while a token is dragged.
        Repeater {
            model: root.zones

            Rectangle {
                id: zone

                required property var modelData
                required property int index

                objectName: "zone_" + modelData.position
                visible: root.dragging
                x: modelData.x * pitch.width
                y: modelData.y * pitch.height
                width: modelData.width * pitch.width
                height: modelData.height * pitch.height
                color: root.hoverZone === index ? Theme.pitch.zoneActive : "transparent"
                border.width: Theme.borderWidth
                border.color: Theme.pitch.zoneBorder

                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: Theme.spacing.xs
                    text: zone.modelData.text
                    color: Theme.pitch.line
                    font.pixelSize: Theme.fontSize.caption
                    font.weight: root.hoverZone === zone.index ? Font.Bold : Font.Normal
                }
            }
        }

        // Below the tokens: a token dropped on another one swaps, anywhere else it moves.
        DropArea {
            id: pitchDrop

            objectName: "pitchDropArea"
            anchors.fill: parent
            keys: ["fdss-slot"]
            enabled: root.interactive
            onDropped: drop => {
                const payload = (drop.source as PlayerToken)?.payload;
                if (payload && payload.kind === "slot")
                    root.slotMoved(payload.slot, Math.max(0, Math.min(1, drop.x / pitch.width)), Math.max(0, Math.min(1, drop.y / pitch.height)));
                drop.accept();
            }
        }

        Repeater {
            id: slots

            model: root.model

            delegate: Item {
                id: slotItem

                required property int slotIndex
                required property string positionText
                required property real pitchX
                required property real pitchY
                required property var shirtNumber
                required property string shortName
                required property string fullName
                required property string severity
                required property string issueText
                required property bool selected
                required property var playerId
                readonly property alias token: slotToken

                // The circle on the point of the slot; the whole token stays on the pitch.
                x: Math.max(0, Math.min(pitch.width - width, pitchX * pitch.width - width / 2))
                y: Math.max(0, Math.min(pitch.height - height, pitchY * pitch.height - Theme.pitch.tokenSize / 2))
                width: slotToken.implicitWidth
                height: slotToken.implicitHeight
                z: slotToken.activeFocus || slotDrop.containsDrag ? 2 : 1

                PlayerToken {
                    id: slotToken

                    objectName: "token_" + slotItem.slotIndex
                    anchors.fill: parent
                    positionText: slotItem.positionText
                    shirtNumber: slotItem.shirtNumber
                    shortName: slotItem.shortName
                    fullName: slotItem.fullName
                    severity: slotItem.severity
                    issueText: slotItem.issueText
                    selected: slotItem.selected
                    interactive: root.interactive
                    onActivated: root.slotActivated(slotItem.slotIndex, false)
                    onTapped: root.slotActivated(slotItem.slotIndex, true)
                    onDoubleTapped: root.slotDoubleTapped(slotItem.slotIndex)
                    onDeletePressed: {
                        if (root.interactive)
                            root.removeRequested(slotItem.slotIndex);
                    }
                    onBenchPressed: {
                        if (root.interactive)
                            root.benchRequested(slotItem.slotIndex);
                    }
                    onDragActiveChanged: {
                        if (dragActive)
                            root.startDrag(slotToken, {
                                "kind": "slot",
                                "slot": slotItem.slotIndex,
                                "playerId": slotItem.playerId
                            });
                    }
                    onDragScenePositionChanged: {
                        if (dragActive)
                            root.moveGhost(dragScenePosition);
                    }
                    onDragReleased: root.finishDrag()
                }

                DropArea {
                    id: slotDrop

                    anchors.fill: parent
                    keys: ["fdss-slot", "fdss-player"]
                    enabled: root.interactive && !slotToken.dragActive
                    onDropped: drop => {
                        const payload = (drop.source as PlayerToken)?.payload;
                        if (payload && payload.kind === "slot")
                            root.slotsSwapped(payload.slot, slotItem.slotIndex);
                        else if (payload)
                            root.playerDropped(slotItem.slotIndex, payload.playerId);
                        drop.accept();
                    }
                }

                // Where a drop would land.
                Rectangle {
                    visible: slotDrop.containsDrag
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.pitch.tokenSize + 2 * Theme.spacing.xs
                    height: width
                    y: -Theme.spacing.xs
                    radius: width / 2
                    color: "transparent"
                    border.width: 2 * Theme.focusRingWidth
                    border.color: Theme.accent
                }
            }
        }
    }

    // The copy that follows the pointer; in the overlay, so no view clips it.
    PlayerToken {
        id: ghost

        objectName: "pitchDragGhost"
        parent: Overlay.overlay
        ghost: true
        visible: false
        z: 1
        width: implicitWidth
        height: implicitHeight
        Drag.active: visible
        Drag.keys: ["fdss-slot"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: Theme.pitch.tokenSize / 2
    }
}
