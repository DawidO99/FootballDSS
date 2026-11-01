pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// ListView-based table with a sticky sortable header, alternating rows, keyboard navigation
// (Up/Down/Home/End/PageUp/PageDown, Enter = activated) and an optional empty state.
// Sorting is performed by the viewmodel in response to sortRequested(role, ascending).
// API:
//   model: var              QAbstractListModel, ListModel or JS array of row objects
//   columns: var            [{ role, title, width (<=1 fraction or >1 px), align?, delegate?, sortable? }]
//   currentIndex: int       selected row index (-1 = none)
//   sortRole: string        currently sorted column role
//   sortAscending: bool     sort direction indicator in the header
//   emptyState: Component   custom empty state shown when count === 0
//   touch: bool             44 px row height
//   count: int              read-only row count
//   sortRequested(string role, bool ascending)
//   activated(int index)    Enter or double-click on a row
FocusScope {
    id: root

    property var model
    property var columns: []
    property alias currentIndex: listView.currentIndex
    property string sortRole
    property bool sortAscending: true
    property Component emptyState
    property bool touch: false
    readonly property int count: listView.count
    readonly property int effectiveRowHeight: touch ? Theme.controlHeightTouch : Theme.rowHeight

    signal sortRequested(string role, bool ascending)
    signal activated(int index)

    function columnWidth(column, totalWidth) {
        let fixedSum = 0;
        let fractionSum = 0;
        for (let i = 0; i < root.columns.length; ++i) {
            const w = root.columns[i].width ?? 1;
            if (w > 1)
                fixedSum += w;
            else
                fractionSum += w;
        }
        const widthSpec = column.width ?? 1;
        if (widthSpec > 1)
            return widthSpec;
        const remaining = Math.max(0, totalWidth - fixedSum);
        return fractionSum > 0 ? remaining * (widthSpec / fractionSum) : 0;
    }

    function moveSelection(targetIndex) {
        if (listView.count <= 0)
            return;
        const clamped = Math.max(0, Math.min(listView.count - 1, targetIndex));
        listView.currentIndex = clamped;
        listView.positionViewAtIndex(clamped, ListView.Contain);
    }

    function pageStep() {
        return Math.max(1, Math.floor(listView.height / root.effectiveRowHeight));
    }

    implicitWidth: 2 * Theme.fieldWidth
    implicitHeight: Theme.tableHeaderHeight + 6 * effectiveRowHeight
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.m
        color: Theme.surface
        border.width: listView.activeFocus ? Theme.focusRingWidth : Theme.borderWidth
        border.color: listView.activeFocus ? Theme.accent : Theme.border
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.borderWidth
            spacing: 0

            Rectangle {
                id: headerBar

                Layout.fillWidth: true
                Layout.preferredHeight: Theme.tableHeaderHeight
                color: Theme.surfaceAlt

                Row {
                    anchors.fill: parent

                    Repeater {
                        model: root.columns

                        delegate: AbstractButton {
                            id: headerButton

                            required property var modelData
                            required property int index
                            readonly property bool sortable: modelData.sortable !== false
                            readonly property bool sortedHere: root.sortRole === modelData.role

                            width: root.columnWidth(modelData, headerBar.width)
                            height: headerBar.height
                            enabled: sortable
                            focusPolicy: Qt.NoFocus
                            hoverEnabled: true
                            text: modelData.title ?? ""
                            Accessible.role: Accessible.ColumnHeader
                            Accessible.name: text
                            onClicked: {
                                const nextAsc = sortedHere ? !root.sortAscending : true;
                                root.sortRole = modelData.role;
                                root.sortAscending = nextAsc;
                                root.sortRequested(modelData.role, nextAsc);
                            }

                            background: Rectangle {
                                color: headerButton.hovered && headerButton.sortable ? Theme.rowHover : "transparent"
                            }

                            contentItem: RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacing.m
                                anchors.rightMargin: Theme.spacing.m
                                spacing: Theme.spacing.xs

                                Label {
                                    Layout.fillWidth: true
                                    text: headerButton.text
                                    color: headerButton.sortedHere ? Theme.text : Theme.textMuted
                                    font.pixelSize: Theme.fontSize.caption
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: headerButton.modelData.align ?? Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                Icon {
                                    visible: headerButton.sortedHere
                                    name: root.sortAscending ? "chevronUp" : "chevronDown"
                                    size: Theme.iconSize.s
                                    color: Theme.accent
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Theme.borderWidth
                    color: Theme.border
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: listView

                    anchors.fill: parent
                    clip: true
                    focus: true
                    model: root.model
                    boundsBehavior: Flickable.StopAtBounds
                    Accessible.role: Accessible.Table
                    Accessible.name: root.Accessible.name

                    Keys.onUpPressed: root.moveSelection((currentIndex < 0 ? 0 : currentIndex) - 1)
                    Keys.onDownPressed: root.moveSelection(currentIndex + 1)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Home) {
                            root.moveSelection(0);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_End) {
                            root.moveSelection(listView.count - 1);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_PageUp) {
                            root.moveSelection((listView.currentIndex < 0 ? 0 : listView.currentIndex) - root.pageStep());
                            event.accepted = true;
                        } else if (event.key === Qt.Key_PageDown) {
                            root.moveSelection((listView.currentIndex < 0 ? 0 : listView.currentIndex) + root.pageStep());
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            if (listView.currentIndex >= 0 && listView.currentIndex < listView.count) {
                                root.activated(listView.currentIndex);
                                event.accepted = true;
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        id: rowItem

                        required property var model
                        required property int index
                        readonly property var record: model.modelData !== undefined ? model.modelData : model
                        readonly property bool selected: ListView.isCurrentItem

                        width: listView.width
                        height: root.effectiveRowHeight
                        padding: 0
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        Accessible.role: Accessible.Row
                        onClicked: {
                            listView.forceActiveFocus(Qt.MouseFocusReason);
                            listView.currentIndex = index;
                        }
                        onDoubleClicked: {
                            listView.forceActiveFocus(Qt.MouseFocusReason);
                            listView.currentIndex = index;
                            root.activated(index);
                        }

                        background: Rectangle {
                            color: rowItem.selected ? Theme.selection : rowItem.hovered ? Theme.rowHover : rowItem.index % 2 === 1 ? Theme.rowAlt : Theme.surface

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: Theme.borderWidth
                                color: Theme.border
                                opacity: Theme.disabledOpacity
                            }
                        }

                        contentItem: Row {
                            Repeater {
                                model: root.columns

                                delegate: Item {
                                    id: cell

                                    required property var modelData
                                    readonly property var columnData: modelData
                                    readonly property var cellValue: rowItem.record ? rowItem.record[columnData.role] : undefined
                                    readonly property bool hasCustomDelegate: columnData.delegate !== undefined && columnData.delegate !== null

                                    function syncCustomCell() {
                                        const custom = cellLoader.item as DataTableCell;
                                        if (!custom)
                                            return;
                                        custom.value = cell.cellValue;
                                        custom.row = rowItem.index;
                                        custom.rowData = rowItem.record;
                                        custom.selected = rowItem.selected;
                                    }

                                    width: root.columnWidth(columnData, rowItem.width)
                                    height: rowItem.height
                                    onCellValueChanged: syncCustomCell()

                                    Connections {
                                        target: rowItem

                                        function onIndexChanged() {
                                            cell.syncCustomCell();
                                        }

                                        function onRecordChanged() {
                                            cell.syncCustomCell();
                                        }

                                        function onSelectedChanged() {
                                            cell.syncCustomCell();
                                        }
                                    }

                                    Label {
                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.spacing.m
                                        anchors.rightMargin: Theme.spacing.m
                                        visible: !cell.hasCustomDelegate
                                        text: cell.cellValue !== undefined && cell.cellValue !== null ? String(cell.cellValue) : ""
                                        color: Theme.text
                                        font.pixelSize: Theme.fontSize.body
                                        horizontalAlignment: cell.columnData.align ?? Text.AlignLeft
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }

                                    Loader {
                                        id: cellLoader

                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.spacing.m
                                        anchors.rightMargin: Theme.spacing.m
                                        active: cell.hasCustomDelegate
                                        sourceComponent: cell.columnData.delegate ?? null
                                        onLoaded: cell.syncCustomCell()
                                    }
                                }
                            }
                        }
                    }
                }

                Loader {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing.xl
                    active: listView.count === 0
                    sourceComponent: root.emptyState ?? defaultEmptyState
                }

                Component {
                    id: defaultEmptyState

                    EmptyState {
                        title: qsTr("No rows to display")
                    }
                }
            }
        }
    }
}

