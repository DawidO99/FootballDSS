pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// ListView-based table with a sticky sortable header, alternating rows, keyboard navigation
// (Up/Down/Home/End/PageUp/PageDown, Enter = activated) and an optional empty state.
// Sorting is performed by the viewmodel in response to sortRequested(role, ascending).
// Tab reaches the sortable headers (Enter/Space request the sort), then the rows.
// Columns always fit the table: pixel columns shrink proportionally when they do not fit beside the
// fraction columns' minimum width.
// Every view passes its own emptyState that says what to do next, with a button (AGENTS.md section
// 6). The built-in one has no action and only keeps a forgotten emptyState from showing a blank table.
// API:
//   model: var              QAbstractListModel, ListModel or JS array of row objects
//   columns: var            [{ role, title, width (<=1 fraction or >1 px), align?, delegate?, sortable? }]
//   currentIndex: int       selected row index (-1 = none)
//   sortRole: string        input: sorted column role; never assigned by the table
//   sortAscending: bool     input: sort direction; never assigned by the table
//   currentSortRole, currentSortAscending   read-only: what the header shows, the inputs or the
//                           user's last request since they changed
//   emptyState: Component   empty state shown when count === 0
//   touch: bool             44 px header and row height
//   count: int              read-only row count
//   columnWidths: var       read-only width of each column in px
//   sortRequested(string role, bool ascending)
//   activated(int index)    Enter or double-click on a row
// A sort request stays inside the table, so bindings on sortRole and sortAscending survive it. The
// viewmodel sorts in sortRequested and updates them; whenever they change the header shows them.
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
    readonly property string currentSortRole: internal.sortRole
    readonly property bool currentSortAscending: internal.sortAscending
    readonly property int effectiveRowHeight: touch ? Theme.controlHeightTouch : Theme.rowHeight
    readonly property int effectiveHeaderHeight: touch ? Math.max(Theme.tableHeaderHeight, Theme.hitTouch) : Theme.tableHeaderHeight
    readonly property var columnWidths: computeColumnWidths(columns, width - 2 * Theme.borderWidth)

    signal sortRequested(string role, bool ascending)
    signal activated(int index)

    function computeColumnWidths(columns, totalWidth) {
        const available = Math.max(0, totalWidth);
        const specs = columns.map(column => column.width ?? 1);
        const fixedSum = specs.filter(spec => spec > 1).reduce((sum, spec) => sum + spec, 0);
        const fractionCount = specs.filter(spec => spec <= 1).length;

        // When even the minimums do not fit, the fraction columns share the whole width equally.
        const minimum = fractionCount > 0 ? Math.min(Theme.spacing.xxl * 2, available / fractionCount) : 0;
        const roomForFixed = available - fractionCount * minimum;
        const fixedScale = fixedSum > roomForFixed ? roomForFixed / fixedSum : 1;

        // Fractions below the minimum are pinned to it and the rest share what remains; pinning one
        // column can push another below the minimum, hence the loop.
        const widths = specs.map(spec => spec > 1 ? spec * fixedScale : -1);
        for (;;) {
            let remaining = available;
            let fractionSum = 0;
            for (let i = 0; i < specs.length; ++i) {
                if (widths[i] >= 0)
                    remaining -= widths[i];
                else
                    fractionSum += specs[i];
            }
            const free = specs.map((spec, i) => i).filter(i => widths[i] < 0);
            if (free.length === 0)
                return widths;
            const share = i => fractionSum > 0 ? remaining * specs[i] / fractionSum : remaining / free.length;
            const pinned = free.filter(i => share(i) < minimum);
            if (pinned.length === 0) {
                for (const i of free)
                    widths[i] = share(i);
                return widths;
            }
            for (const i of pinned)
                widths[i] = minimum;
        }
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
    implicitHeight: effectiveHeaderHeight + 6 * effectiveRowHeight
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    onSortRoleChanged: internal.sortRole = root.sortRole
    onSortAscendingChanged: internal.sortAscending = root.sortAscending
    Component.onCompleted: {
        internal.sortRole = root.sortRole;
        internal.sortAscending = root.sortAscending;
    }

    QtObject {
        id: internal

        property string sortRole
        property bool sortAscending: true
    }

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
                Layout.preferredHeight: root.effectiveHeaderHeight
                color: Theme.surfaceAlt

                Row {
                    anchors.fill: parent

                    Repeater {
                        // By count: retranslating qsTr() titles must not rebuild the headers.
                        model: root.columns.length

                        delegate: AbstractButton {
                            id: headerButton

                            required property int index
                            readonly property var column: root.columns[index] ?? ({})
                            readonly property bool sortable: column.sortable !== false
                            readonly property bool sortedHere: internal.sortRole === column.role

                            function requestSort() {
                                const nextAsc = sortedHere ? !internal.sortAscending : true;
                                internal.sortRole = column.role;
                                internal.sortAscending = nextAsc;
                                root.sortRequested(column.role, nextAsc);
                            }

                            objectName: "header_" + column.role
                            width: root.columnWidths[index] ?? 0
                            height: headerBar.height
                            enabled: sortable
                            // Tab only: a click on a header leaves keyboard focus on the rows.
                            focusPolicy: sortable ? Qt.TabFocus : Qt.NoFocus
                            hoverEnabled: true
                            text: column.title ?? ""
                            Accessible.role: Accessible.ColumnHeader
                            Accessible.name: text
                            onClicked: requestSort()
                            Keys.onReturnPressed: requestSort()
                            Keys.onEnterPressed: requestSort()

                            background: Rectangle {
                                color: headerButton.hovered && headerButton.sortable ? Theme.rowHover : "transparent"
                                border.width: headerButton.activeFocus ? Theme.focusRingWidth : 0
                                border.color: Theme.accent
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
                                    horizontalAlignment: headerButton.column.align ?? Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                Icon {
                                    visible: headerButton.sortedHere
                                    name: internal.sortAscending ? "chevronUp" : "chevronDown"
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

                    objectName: "tableRows"
                    anchors.fill: parent
                    clip: true
                    focus: true
                    activeFocusOnTab: true
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
                                model: root.columns.length

                                delegate: Item {
                                    id: cell

                                    required property int index
                                    readonly property var columnData: root.columns[index] ?? ({})
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

                                    width: root.columnWidths[index] ?? 0
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

