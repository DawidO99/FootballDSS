pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The squad to pick from on the tactics board (P12): the XI from the goalkeeper up the pitch, the
// bench in its order, then the rest in the order of the header (number, name, position, status,
// load; a click on the active one turns the direction). The model sorts (LineupPlayersSortModel);
// the list only asks. A row shows the number, the name, the positions and where the player is, the
// availability on the match day when it is not "available" and the load status (P11), and two
// actions: XI (into the selected slot, or the first free one; Enter) and Bench (B). A click selects
// the row, a double click opens the player card. Keyboard path: Tab to the list, the arrows, Enter
// or B; after a player leaves the rest, the next one there is current, so Enter after Enter fills
// the lineup. A row can be dragged on a token (Drag.keys "fdss-player", source.payload { kind:
// "player", playerId }); a token or a bench player dropped on the list leaves the lineup. Rows are
// Theme.hitTouch high.
// API:
//   model: LineupPlayersSortModel  playerId, shirtNumber, name, shortName, positionsText,
//                                statusKey, statusText, loadStatusKey, loadStatusText, placement,
//                                placementText, severity, issueText, detailsText, section,
//                                sectionText; get(row), rowOf(playerId)
//   sortColumn: string           number, name, position, status or load
//   ascending: bool
//   interactive: bool            false: read-only (the card still opens)
//   pickRequested(playerId)      the XI action or Enter
//   benchRequested(playerId)     the Bench action or B
//   removeRequested(playerId)    a token or bench player dropped here
//   cardRequested(playerId)      double click
//   sortRequested(column)
//   focusList()
Item {
    id: root

    property var model
    property string sortColumn: "number"
    property bool ascending: true
    property bool interactive: true
    readonly property alias listView: list

    signal pickRequested(double playerId)
    signal benchRequested(double playerId)
    signal removeRequested(double playerId)
    signal cardRequested(double playerId)
    signal sortRequested(string column)

    function focusList() {
        if (list.currentIndex < 0 && list.count > 0)
            list.currentIndex = 0;
        list.forceActiveFocus(Qt.TabFocusReason);
    }

    // Runs an action on a row. A player who leaves the rest makes the next one of the rest current;
    // a player who stays where they were stays current.
    function act(row, action) {
        if (!root.interactive || row < 0 || row >= list.count)
            return;
        const entry = root.model.get(row);
        const below = row + 1 < list.count ? root.model.get(row + 1) : null;
        const next = entry.section === 2 && below && below.section === 2 ? below.playerId : entry.playerId;
        action(entry.playerId);
        if (!root.model.rowOf)
            return;
        const stayed = root.model.rowOf(entry.playerId);
        const target = stayed >= 0 && root.model.get(stayed).section === entry.section ? stayed : root.model.rowOf(next);
        list.currentIndex = target >= 0 ? target : Math.min(row, list.count - 1);
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

    // A small action of a row: a word, touch sized, with the shortcut in the tooltip.
    component RowAction: AbstractButton {
        id: rowAction

        property string tip

        implicitWidth: Math.max(Theme.hitTouch, rowActionLabel.implicitWidth + 2 * Theme.spacing.s)
        implicitHeight: Theme.hitTouch
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        opacity: enabled ? 1.0 : Theme.disabledOpacity
        Accessible.role: Accessible.Button
        Accessible.name: rowAction.tip
        ToolTip.visible: hovered
        ToolTip.text: rowAction.tip
        ToolTip.delay: Theme.tooltipDelay

        background: Rectangle {
            anchors.fill: parent
            anchors.topMargin: Theme.spacing.xs
            anchors.bottomMargin: Theme.spacing.xs
            radius: Theme.radius.m
            color: rowAction.down ? Theme.border : rowAction.hovered ? Theme.surfaceAlt : "transparent"
            border.width: Theme.borderWidth
            border.color: Theme.border
        }

        contentItem: Label {
            id: rowActionLabel

            text: rowAction.text
            color: Theme.text
            font.pixelSize: Theme.fontSize.caption
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.m
        color: Theme.surface
        border.width: listDrop.containsDrag || list.activeFocus ? Theme.focusRingWidth : Theme.borderWidth
        border.color: listDrop.containsDrag || list.activeFocus ? Theme.accent : Theme.border
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.borderWidth
            spacing: 0

            // The sort header, as in Squad.
            Rectangle {
                objectName: "playerSortHeader"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.hitMin
                color: Theme.surfaceAlt

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing.xs
                    anchors.rightMargin: Theme.spacing.xs
                    spacing: 0

                    Repeater {
                        model: [
                            {
                                "column": "number",
                                "title": qsTr("No."),
                                "fill": false
                            },
                            {
                                "column": "name",
                                "title": qsTr("Name"),
                                "fill": true
                            },
                            {
                                "column": "position",
                                "title": qsTr("Position"),
                                "fill": false
                            },
                            {
                                "column": "status",
                                "title": qsTr("Status"),
                                "fill": false
                            },
                            {
                                "column": "load",
                                "title": qsTr("Load"),
                                "fill": false
                            }
                        ]

                        delegate: AbstractButton {
                            id: header

                            required property var modelData
                            readonly property bool sortedHere: root.sortColumn === modelData.column

                            objectName: "sortHeader_" + modelData.column
                            Layout.fillWidth: modelData.fill
                            Layout.fillHeight: true
                            implicitWidth: headerRow.implicitWidth + 2 * Theme.spacing.s
                            focusPolicy: Qt.TabFocus
                            hoverEnabled: true
                            text: modelData.title
                            Accessible.role: Accessible.ColumnHeader
                            Accessible.name: text
                            onClicked: root.sortRequested(modelData.column)
                            Keys.onReturnPressed: root.sortRequested(modelData.column)
                            Keys.onEnterPressed: root.sortRequested(modelData.column)

                            background: Rectangle {
                                color: header.hovered ? Theme.rowHover : "transparent"
                                border.width: header.activeFocus ? Theme.focusRingWidth : 0
                                border.color: Theme.accent
                            }

                            contentItem: Item {
                                RowLayout {
                                    id: headerRow

                                    anchors.verticalCenter: parent.verticalCenter
                                    x: Theme.spacing.s
                                    spacing: Theme.spacing.xs

                                    Label {
                                        text: header.text
                                        color: header.sortedHere ? Theme.text : Theme.textMuted
                                        font.pixelSize: Theme.fontSize.caption
                                        font.weight: Font.DemiBold
                                    }

                                    Icon {
                                        visible: header.sortedHere
                                        name: root.ascending ? "chevronUp" : "chevronDown"
                                        size: Theme.iconSize.s
                                        color: Theme.accent
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ListView {
                id: list

                objectName: "playerListView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.model
                activeFocusOnTab: count > 0
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Squad")
                Keys.onReturnPressed: root.act(list.currentIndex, playerId => root.pickRequested(playerId))
                Keys.onEnterPressed: root.act(list.currentIndex, playerId => root.pickRequested(playerId))
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_B && event.modifiers === Qt.NoModifier && root.interactive) {
                        root.act(list.currentIndex, playerId => root.benchRequested(playerId));
                        event.accepted = true;
                    }
                }

                section.property: "sectionText"
                section.criteria: ViewSection.FullString
                section.delegate: Rectangle {
                    id: sectionHeading

                    required property string section

                    width: ListView.view.width
                    height: Theme.hitMin
                    color: Theme.surface

                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing.s
                        anchors.verticalCenter: parent.verticalCenter
                        text: sectionHeading.section
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                        font.weight: Font.DemiBold
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: Theme.borderWidth
                        color: Theme.border
                    }
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
                        anchors.rightMargin: Theme.spacing.s
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
                            name: row.severity === "error" ? "danger" : row.severity === "warning" ? "warn" : "info"
                            color: row.severity === "error" ? Theme.danger : row.severity === "warning" ? Theme.warn : Theme.info
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

                        RowAction {
                            objectName: "xiButton_" + row.index
                            visible: root.interactive
                            text: qsTr("XI")
                            tip: qsTr("Put in the selected slot, or the first free one (Enter)")
                            onClicked: {
                                list.currentIndex = row.index;
                                root.act(row.index, playerId => root.pickRequested(playerId));
                            }
                        }

                        RowAction {
                            objectName: "benchButton_" + row.index
                            visible: root.interactive
                            enabled: row.placement !== "bench"
                            text: qsTr("Bench")
                            tip: qsTr("Add to the bench (B)")
                            onClicked: {
                                list.currentIndex = row.index;
                                root.act(row.index, playerId => root.benchRequested(playerId));
                            }
                        }
                    }

                    HoverHandler {
                        id: rowHover
                    }

                    TapHandler {
                        onSingleTapped: {
                            list.currentIndex = row.index;
                            list.forceActiveFocus(Qt.MouseFocusReason);
                        }
                        onDoubleTapped: root.cardRequested(row.playerId)
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
                        text: [row.name, row.details, row.detailsText, row.issueText, qsTr("Double click: the player card.")].filter(part => part.length > 0).join("\n")
                        delay: Theme.tooltipDelay
                    }
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
