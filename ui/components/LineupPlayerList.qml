pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The squad to pick from on the tactics board (P12): number, name, positions, where the player is
// (XI or bench), the availability on the match day when it is not "available", and the load status
// (P11). Keyboard path: the list takes the focus with Tab, the arrows move, Enter puts the player in
// the selected slot (a click or tap does the same), B puts them on the bench (the accelerator of the
// bench button of the row). The tooltip names the days: availability on the match day, load as of
// its day. A row can be dragged on a token (Drag.keys
// "fdss-player", source.payload { kind: "player", playerId }); a token or a bench player dropped on
// the list leaves the lineup. Rows are Theme.hitTouch high.
// API:
//   model: LineupPlayersModel    playerId, shirtNumber, name, shortName, positionsText, statusKey,
//                                statusText, loadStatusKey, loadStatusText, placement,
//                                placementText, severity, issueText, detailsText
//   interactive: bool            false: read-only
//   pickRequested(playerId)      Enter, click or tap
//   benchRequested(playerId)     the bench button of a row
//   removeRequested(playerId)    a token or bench player dropped here
//   focusList()
Item {
    id: root

    property var model
    property bool interactive: true
    readonly property alias listView: list

    signal pickRequested(double playerId)
    signal benchRequested(double playerId)
    signal removeRequested(double playerId)

    function focusList() {
        if (list.currentIndex < 0 && list.count > 0)
            list.currentIndex = 0;
        list.forceActiveFocus(Qt.TabFocusReason);
    }

    function moveGhost(scenePosition) {
        if (!ghost.visible || !ghost.parent)
            return;
        const point = ghost.parent.mapFromItem(null, scenePosition.x, scenePosition.y);
        ghost.x = point.x - ghost.Drag.hotSpot.x;
        ghost.y = point.y - ghost.Drag.hotSpot.y;
    }

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.popupMaxHeight

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.m
        color: Theme.surface
        border.width: listDrop.containsDrag || list.activeFocus ? Theme.focusRingWidth : Theme.borderWidth
        border.color: listDrop.containsDrag || list.activeFocus ? Theme.accent : Theme.border

        ListView {
            id: list

            objectName: "playerListView"
            anchors.fill: parent
            anchors.margins: Theme.borderWidth
            clip: true
            model: root.model
            activeFocusOnTab: count > 0
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Squad")
            Keys.onReturnPressed: list.pickCurrent()
            Keys.onEnterPressed: list.pickCurrent()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_B && event.modifiers === Qt.NoModifier && root.interactive && list.currentIndex >= 0) {
                    root.benchRequested(root.model.get(list.currentIndex).playerId);
                    event.accepted = true;
                }
            }

            function pickCurrent() {
                if (root.interactive && list.currentIndex >= 0)
                    root.pickRequested(root.model.get(list.currentIndex).playerId);
            }

            delegate: Rectangle {
                id: row

                required property int index
                required property var playerId
                required property var shirtNumber
                required property string name
                required property string shortName
                required property string positionsText
                required property string statusKey
                required property string statusText
                required property string loadStatusKey
                required property string loadStatusText
                required property string placement
                required property string placementText
                required property string severity
                required property string issueText
                required property string detailsText
                readonly property string details: [row.positionsText, row.placementText].filter(part => part.length > 0).join(" · ")

                objectName: "playerRow_" + row.index
                width: ListView.view.width
                height: Theme.hitTouch
                color: ListView.isCurrentItem && list.activeFocus ? Theme.selection : rowHover.hovered ? Theme.rowHover : "transparent"
                opacity: rowDrag.active ? Theme.disabledOpacity : 1.0
                Accessible.role: Accessible.ListItem
                Accessible.name: [row.name, row.details, row.statusKey !== "available" ? row.statusText : "", row.loadStatusText, row.issueText].filter(part => part.length > 0).join(". ")

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing.s
                    spacing: Theme.spacing.s

                    Label {
                        Layout.preferredWidth: Theme.shirtNumberWidth
                        text: row.shirtNumber !== undefined && row.shirtNumber !== null ? String(row.shirtNumber) : ""
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.body
                        horizontalAlignment: Text.AlignRight
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            text: row.name
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.text
                            font.pixelSize: Theme.fontSize.body
                            font.weight: row.placement.length > 0 ? Font.DemiBold : Font.Normal
                        }

                        Label {
                            Layout.fillWidth: true
                            text: row.details
                            elide: Text.ElideRight
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSize.caption
                        }
                    }

                    Icon {
                        visible: row.severity.length > 0
                        name: row.severity === "error" ? "danger" : "warn"
                        color: row.severity === "error" ? Theme.danger : Theme.warn
                        size: Theme.iconSize.s
                    }

                    PlayerStatusBadge {
                        visible: row.statusKey !== "available"
                        statusKey: row.statusKey
                        text: row.statusText
                    }

                    LoadStatusBadge {
                        visible: row.loadStatusKey.length > 0
                        statusKey: row.loadStatusKey
                        text: row.loadStatusText
                    }

                    IconButton {
                        objectName: "benchButton_" + row.index
                        visible: root.interactive
                        enabled: row.placement !== "bench"
                        touch: true
                        iconName: "plus"
                        text: qsTr("Add to bench (B)")
                        focusPolicy: Qt.NoFocus
                        onClicked: root.benchRequested(row.playerId)
                    }
                }

                HoverHandler {
                    id: rowHover
                }

                TapHandler {
                    onTapped: {
                        list.currentIndex = row.index;
                        list.forceActiveFocus(Qt.MouseFocusReason);
                        if (root.interactive)
                            root.pickRequested(row.playerId);
                    }
                }

                DragHandler {
                    id: rowDrag

                    target: null
                    enabled: root.interactive
                    dragThreshold: Theme.pitch.dragThreshold
                    onActiveChanged: {
                        if (active) {
                            ghost.payload = {
                                "kind": "player",
                                "playerId": row.playerId
                            };
                            ghost.shirtNumber = row.shirtNumber;
                            ghost.shortName = row.shortName;
                            ghost.fullName = row.name;
                            ghost.visible = true;
                            root.moveGhost(centroid.scenePosition);
                        } else if (ghost.visible) {
                            ghost.Drag.drop();
                            ghost.visible = false;
                        }
                    }
                    onCentroidChanged: root.moveGhost(centroid.scenePosition)
                }

                PlainToolTip {
                    visible: rowHover.hovered && !rowDrag.active
                    text: [row.name, row.details, row.detailsText, row.issueText, root.interactive ? qsTr("Click or Enter: put in the selected slot.") : ""].filter(part => part.length > 0).join("\n")
                    delay: Theme.tooltipDelay
                }
            }
        }

        DropArea {
            id: listDrop

            objectName: "playerListDropArea"
            anchors.fill: parent
            keys: ["fdss-slot", "fdss-player"]
            enabled: root.interactive
            onDropped: drop => {
                const payload = (drop.source as PlayerToken)?.payload;
                if (payload && (payload.kind === "slot" || payload.kind === "bench") && payload.playerId)
                    root.removeRequested(payload.playerId);
                drop.accept();
            }
        }
    }

    PlayerToken {
        id: ghost

        objectName: "listDragGhost"
        parent: Overlay.overlay
        ghost: true
        visible: false
        z: 1
        width: implicitWidth
        height: implicitHeight
        Drag.active: visible
        Drag.keys: ["fdss-player"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: Theme.pitch.tokenSize / 2
    }
}
