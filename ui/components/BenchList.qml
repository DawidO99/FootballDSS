pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The bench of the tactics board (P12): up to maxCount players in order. A drop target for tokens
// from the pitch and players from the squad list; a bench player can be dragged on a token or back
// to the list (Drag.keys "fdss-player", source.payload { kind: "bench", playerId }). Keyboard: the
// list takes the focus with Tab, the arrows move, Enter puts the player in the selected slot,
// Delete takes them out of the lineup. Rows are Theme.hitTouch high.
// API:
//   model: BenchListModel        benchIndex, playerId, shirtNumber, name, shortName, positionsText,
//                                severity, issueText
//   maxCount: int
//   interactive: bool            false: read-only
//   playerDropped(playerId)      a player from the squad list dropped here
//   slotDropped(slot)            a token from the pitch dropped here
//   pickRequested(playerId)      Enter or a click on a row: into the selected slot
//   removeRequested(playerId)    out of the lineup
//   focusRow(index)
Item {
    id: root

    property var model
    property int maxCount: 12
    property bool interactive: true
    readonly property int count: list.count
    readonly property alias listView: list

    signal playerDropped(double playerId)
    signal slotDropped(int slot)
    signal pickRequested(double playerId)
    signal removeRequested(double playerId)

    function focusRow(index) {
        list.currentIndex = index;
        list.forceActiveFocus(Qt.TabFocusReason);
    }

    function moveGhost(scenePosition) {
        if (!ghost.visible || !ghost.parent)
            return;
        const point = ghost.parent.mapFromItem(null, scenePosition.x, scenePosition.y);
        ghost.x = point.x - ghost.Drag.hotSpot.x;
        ghost.y = point.y - ghost.Drag.hotSpot.y;
    }

    implicitWidth: Theme.benchColumnWidth
    implicitHeight: header.implicitHeight + Theme.spacing.s + root.maxCount * Theme.hitTouch

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing.s

        RowLayout {
            id: header

            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: qsTr("Bench")
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                font.weight: Font.DemiBold
            }

            Label {
                objectName: "benchCount"
                text: qsTr("%1 / %2").arg(list.count).arg(root.maxCount)
                color: list.count >= root.maxCount ? Theme.warn : Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius.m
            color: Theme.surface
            border.width: benchDrop.containsDrag || list.activeFocus ? Theme.focusRingWidth : Theme.borderWidth
            border.color: benchDrop.containsDrag || list.activeFocus ? Theme.accent : Theme.border

            ListView {
                id: list

                objectName: "benchListView"
                anchors.fill: parent
                anchors.margins: Theme.borderWidth
                clip: true
                model: root.model
                activeFocusOnTab: count > 0
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Bench")
                Keys.onReturnPressed: list.pickCurrent()
                Keys.onEnterPressed: list.pickCurrent()
                Keys.onDeletePressed: {
                    if (root.interactive && list.currentIndex >= 0)
                        root.removeRequested(root.model.get(list.currentIndex).playerId);
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
                    required property string severity
                    required property string issueText

                    width: ListView.view.width
                    height: Theme.hitTouch
                    color: ListView.isCurrentItem && list.activeFocus ? Theme.selection : rowHover.hovered ? Theme.rowHover : "transparent"
                    opacity: rowDrag.active ? Theme.disabledOpacity : 1.0
                    Accessible.role: Accessible.ListItem
                    Accessible.name: [row.name, row.positionsText, row.issueText].filter(part => part.length > 0).join(". ")

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
                            }

                            Label {
                                Layout.fillWidth: true
                                text: row.positionsText
                                elide: Text.ElideRight
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                            }
                        }

                        Icon {
                            visible: row.severity.length > 0
                            name: row.severity === "error" ? "danger" : row.severity === "warning" ? "warn" : "info"
                            color: row.severity === "error" ? Theme.danger : row.severity === "warning" ? Theme.warn : Theme.info
                            size: Theme.iconSize.s
                        }

                        IconButton {
                            objectName: "benchRemove_" + row.index
                            visible: root.interactive
                            touch: true
                            iconName: "close"
                            text: qsTr("Take out of the lineup (Del)")
                            focusPolicy: Qt.NoFocus
                            onClicked: root.removeRequested(row.playerId)
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
                                    "kind": "bench",
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
                        text: [row.name, row.positionsText, row.issueText].filter(part => part.length > 0).join("\n")
                        delay: Theme.tooltipDelay
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacing.m
                visible: list.count === 0
                text: root.interactive ? qsTr("Drag players here or use Add to bench in the list.") : qsTr("No players on the bench.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            DropArea {
                id: benchDrop

                objectName: "benchDropArea"
                anchors.fill: parent
                keys: ["fdss-slot", "fdss-player"]
                enabled: root.interactive
                onDropped: drop => {
                    const payload = (drop.source as PlayerToken)?.payload;
                    if (payload && payload.kind === "slot")
                        root.slotDropped(payload.slot);
                    else if (payload && payload.kind === "player")
                        root.playerDropped(payload.playerId);
                    drop.accept();
                }
            }
        }
    }

    PlayerToken {
        id: ghost

        objectName: "benchDragGhost"
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
