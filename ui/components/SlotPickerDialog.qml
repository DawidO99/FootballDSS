pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The players for one slot of the tactics board (P12 manual test, decision 20), in the order and
// groups of the viewmodel: the position of the slot as the main one, as a second one, then the
// other positions; the unavailable ones last in their group, marked by their status. Each row: the
// number, the name, the positions and where the player is (XI, bench or not in the lineup), the
// availability on the match day and the load status (icon and word). The arrows move, Enter, a
// click or a tap picks, Esc closes. Rows are Theme.hitTouch high.
// API:
//   model: SlotPickerModel       playerId, shirtNumber, name, positionsText, group, groupText,
//                                statusKey, statusText, available, loadStatusKey,
//                                loadStatusText, placement, placementText, current
//   title: string
//   picked(playerId)             the dialog closes itself after it
AppDialog {
    id: root

    property alias model: list.model
    readonly property alias listView: list

    signal picked(double playerId)

    function pick(row) {
        if (row < 0 || row >= list.count)
            return;
        const playerId = root.model.get(row).playerId ?? 0;
        // Before closing: whoever listens to closed may end the picker.
        if (playerId > 0) {
            root.picked(playerId);
            root.close();
        }
    }

    // The player of the slot, or the first one.
    function focusStart() {
        let start = 0;
        for (let row = 0; row < list.count; ++row) {
            if (root.model.get(row).current) {
                start = row;
                break;
            }
        }
        list.currentIndex = start;
        list.positionViewAtIndex(start, ListView.Contain);
        list.forceActiveFocus(Qt.TabFocusReason);
    }

    size: AppDialog.Medium
    primaryText: ""
    secondaryText: qsTr("Close")
    onOpened: Qt.callLater(root.focusStart)

    component PickerRow: Rectangle {
        id: row

        required property int index
        required property var playerId
        required property var shirtNumber
        required property string name
        required property string positionsText
        required property string statusKey
        required property string statusText
        required property bool available
        required property string loadStatusKey
        required property string loadStatusText
        required property string placementText
        required property bool current

        objectName: "pickerRow_" + row.index
        width: ListView.view.width
        height: Theme.hitTouch
        color: ListView.isCurrentItem && list.activeFocus ? Theme.selection : rowHover.hovered ? Theme.rowHover : "transparent"
        Accessible.role: Accessible.ListItem
        Accessible.name: [row.name, row.positionsText, row.placementText, row.statusText, row.loadStatusText, row.current ? qsTr("in this slot") : ""].filter(part => part.length > 0).join(". ")

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
                    color: row.available ? Theme.text : Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    font.weight: row.current ? Font.DemiBold : Font.Normal
                }

                Label {
                    Layout.fillWidth: true
                    text: [row.positionsText, row.placementText].filter(part => part.length > 0).join(" · ")
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                }
            }

            Icon {
                visible: row.current
                name: "check"
                color: Theme.accent
                size: Theme.iconSize.s
            }

            PlayerStatusBadge {
                statusKey: row.statusKey
                text: row.statusText
            }

            LoadStatusBadge {
                visible: row.loadStatusKey.length > 0
                statusKey: row.loadStatusKey
                text: row.loadStatusText
            }
        }

        HoverHandler {
            id: rowHover

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: {
                list.currentIndex = row.index;
                root.pick(row.index);
            }
        }
    }

    ListView {
        id: list

        objectName: "slotPickerList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(list.contentHeight, Theme.slotPickerHeight)
        clip: true
        focus: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: root.title
        Keys.onReturnPressed: root.pick(list.currentIndex)
        Keys.onEnterPressed: root.pick(list.currentIndex)

        section.property: "groupText"
        section.criteria: ViewSection.FullString
        section.delegate: Item {
            id: heading

            required property string section

            width: ListView.view.width
            height: Theme.hitMin

            Label {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.s
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacing.xs
                text: heading.section
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
        }

        delegate: PickerRow {}
    }
}
