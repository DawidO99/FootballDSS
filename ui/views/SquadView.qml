pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The squad table and, in the same section, the card of one player.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate(). playerId: opens that card.
    property var navigationArgs: ({})
    property var selectedPlayer: ({})
    readonly property bool hasSelection: selectedPlayer.playerId !== undefined
    readonly property bool canEdit: AppState.can("EditSquad")
    readonly property bool cardOpen: pages.currentIndex === 1
    readonly property alias viewModel: squad
    readonly property alias playerCard: card
    readonly property alias playerEditor: editorDialog

    signal navigateRequested(string key, var args)

    function syncSelection() {
        root.selectedPlayer = squad.rowAt(playersTable.currentIndex);
    }

    // The rows changed under the table: the same player stays selected.
    function restoreSelection() {
        playersTable.currentIndex = squad.rowOf(root.selectedPlayer.playerId ?? 0);
        root.syncSelection();
    }

    function openCard(playerId) {
        card.playerId = playerId;
        card.reload();
        pages.currentIndex = 1;
        card.focusFirst();
    }

    function closeCard() {
        pages.currentIndex = 0;
        playersTable.currentIndex = squad.rowOf(card.playerId);
        root.syncSelection();
        playersTable.forceActiveFocus(Qt.TabFocusReason);
    }

    function openSelectedCard() {
        if (root.hasSelection)
            root.openCard(root.selectedPlayer.playerId);
    }

    function openAddDialog() {
        editorDialog.openForAdding();
    }

    function openEditDialog() {
        if (root.hasSelection)
            editorDialog.openForEditing(root.selectedPlayer.playerId);
    }

    function archiveSelected() {
        if (!root.hasSelection)
            return;
        const id = root.selectedPlayer.playerId;
        const name = root.selectedPlayer.name;
        if (squad.archive(id))
            Toasts.show(qsTr("%1 was archived.").arg(name), qsTr("Undo"), () => squad.unarchive(id));
    }

    function restoreSelected() {
        if (root.hasSelection)
            squad.unarchive(root.selectedPlayer.playerId);
    }

    function clearFilters() {
        searchField.clear();
        squad.clearFilters();
    }

    onNavigationArgsChanged: {
        if (navigationArgs.playerId !== undefined)
            root.openCard(navigationArgs.playerId);
        // From the search of the top bar: the field shows it and keeps the focus for more typing.
        if (navigationArgs.search !== undefined) {
            searchField.setText(navigationArgs.search);
            Qt.callLater(searchField.forceInputFocus);
        }
    }
    // All views stay loaded, and the status of today may have changed since this one was seen.
    onVisibleChanged: {
        if (!visible)
            return;
        squad.reload();
        if (root.cardOpen)
            card.reload();
    }

    SquadViewModel {
        id: squad

        teamId: AppState.teamId
        onErrorOccurred: message => Toasts.error(message)
        onPlayersReloaded: root.restoreSelection()
        onFiltersChanged: root.restoreSelection()
        onSortChanged: root.restoreSelection()
    }

    Shortcut {
        id: addShortcut

        objectName: "addShortcut"
        sequence: "Ctrl+N"
        enabled: root.visible && root.canEdit && !root.cardOpen && !editorDialog.visible
        onActivated: root.openAddDialog()
    }

    StackLayout {
        id: pages

        anchors.fill: parent

        ColumnLayout {
            spacing: Theme.spacing.l

            Item {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacing.xl
                Layout.leftMargin: Theme.spacing.xl
                Layout.rightMargin: Theme.spacing.xl
                implicitHeight: header.implicitHeight

                PageHeader {
                    id: header

                    anchors.fill: parent
                    title: qsTr("Squad")

                    PrimaryButton {
                        visible: root.canEdit
                        text: qsTr("Add player")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Add player (%1)").arg(addShortcut.nativeText)
                        ToolTip.delay: Theme.tooltipDelay
                        onClicked: root.openAddDialog()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacing.xl
                Layout.rightMargin: Theme.spacing.xl
                spacing: Theme.spacing.m

                SearchField {
                    id: searchField

                    placeholderText: qsTr("Search by name or number…")
                    Accessible.name: qsTr("Search players")
                    onTextChanged: squad.searchText = text
                }

                SegmentedControl {
                    model: squad.statusGroupOptions
                    currentValue: squad.statusGroup
                    Accessible.name: qsTr("Status filter")
                    onActivated: value => squad.statusGroup = value
                }

                SelectField {
                    textRole: "text"
                    valueRole: "value"
                    model: squad.positionGroupOptions
                    value: squad.positionGroup
                    Accessible.name: qsTr("Position filter")
                    onValueActivated: value => squad.positionGroup = value
                }

                CheckBoxField {
                    text: qsTr("Show archived")
                    checked: squad.showArchived
                    onToggled: squad.showArchived = checked
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            DataTable {
                id: playersTable

                objectName: "playersTable"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.spacing.xl
                Layout.rightMargin: Theme.spacing.xl
                focus: true
                Accessible.name: qsTr("Squad")
                model: squad.players
                sortRole: squad.sortRole
                sortAscending: squad.sortAscending
                columns: [
                    {
                        "role": "number",
                        "title": qsTr("No."),
                        "width": 0.08
                    },
                    {
                        "role": "name",
                        "title": qsTr("Name"),
                        "width": 0.37
                    },
                    {
                        "role": "position",
                        "title": qsTr("Position"),
                        "width": 0.2,
                        "delegate": positionCell
                    },
                    {
                        "role": "age",
                        "title": qsTr("Age"),
                        "width": 0.1
                    },
                    {
                        "role": "status",
                        "title": qsTr("Status"),
                        "width": 0.25,
                        "delegate": statusCell
                    }
                ]
                emptyState: playersEmptyState
                onSortRequested: (role, ascending) => squad.sortBy(role, ascending)
                onCurrentIndexChanged: root.syncSelection()
                onActivated: index => root.openCard(squad.rowAt(index).playerId)
            }

            Component {
                id: positionCell

                DataTableCell {
                    id: cell

                    // Many secondary positions end at the edge of the column; the tooltip has them all.
                    clip: true

                    HoverHandler {
                        id: positionHover
                    }

                    Row {
                        height: cell.height
                        spacing: Theme.spacing.s

                        Label {
                            height: parent.height
                            text: String(cell.value ?? "")
                            color: Theme.text
                            font.pixelSize: Theme.fontSize.body
                            verticalAlignment: Text.AlignVCenter
                        }

                        Row {
                            height: parent.height

                            Repeater {
                                model: cell.rowData ? cell.rowData.secondaryPositions : []

                                delegate: Row {
                                    id: secondaryCode

                                    required property var modelData
                                    required property int index

                                    height: cell.height

                                    Label {
                                        height: parent.height
                                        visible: secondaryCode.index > 0
                                        //: Between the codes of the secondary positions of a player.
                                        text: qsTr("·")
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontSize.caption
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    Label {
                                        objectName: "secondaryCode_" + secondaryCode.modelData.code
                                        height: parent.height
                                        text: secondaryCode.modelData.code
                                        color: secondaryCode.modelData.highlighted ? Theme.accent : Theme.textMuted
                                        font.pixelSize: Theme.fontSize.caption
                                        font.weight: secondaryCode.modelData.highlighted ? Font.DemiBold : Font.Normal
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }

                    PlainToolTip {
                        visible: positionHover.hovered
                        text: cell.rowData ? cell.rowData.positionTooltip : ""
                        delay: Theme.tooltipDelay
                    }
                }
            }

            Component {
                id: statusCell

                DataTableCell {
                    id: cell

                    PlayerStatusBadge {
                        anchors.verticalCenter: parent.verticalCenter
                        statusKey: cell.rowData ? cell.rowData.statusKey : ""
                        text: String(cell.value ?? "")
                    }
                }
            }

            Component {
                id: playersEmptyState

                EmptyState {
                    readonly property bool failed: squad.loadError.length > 0

                    iconName: "squad"
                    title: failed ? qsTr("The squad could not be read") : squad.filtersActive ? qsTr("No players match the filters") : qsTr("No active players yet")
                    description: failed ? squad.loadError : squad.filtersActive ? qsTr("Change the search or the filters to see more players.") : root.canEdit ? qsTr("Add the players of your team to start.") : qsTr("Ask the coach or an assistant to add the players.")
                    actionText: failed ? qsTr("Try again") : squad.filtersActive ? qsTr("Clear filters") : root.canEdit ? qsTr("Add player") : ""
                    onActionTriggered: {
                        if (failed)
                            squad.reload();
                        else if (squad.filtersActive)
                            root.clearFilters();
                        else
                            root.openAddDialog();
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacing.xl
                Layout.rightMargin: Theme.spacing.xl
                Layout.bottomMargin: Theme.spacing.xl
                spacing: Theme.spacing.m

                SecondaryButton {
                    enabled: root.hasSelection
                    text: qsTr("Open card")
                    onClicked: root.openSelectedCard()
                }

                SecondaryButton {
                    visible: root.canEdit
                    enabled: root.hasSelection
                    text: qsTr("Edit player")
                    onClicked: root.openEditDialog()
                }

                DangerButton {
                    visible: root.canEdit && !root.selectedPlayer.archived
                    enabled: root.hasSelection
                    text: qsTr("Archive")
                    onClicked: root.archiveSelected()
                }

                SecondaryButton {
                    visible: root.canEdit && root.selectedPlayer.archived === true
                    text: qsTr("Restore")
                    onClicked: root.restoreSelected()
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        PlayerCardView {
            id: card

            shortcutsEnabled: !editorDialog.visible
            onBackRequested: root.closeCard()
            onEditRequested: playerId => editorDialog.openForEditing(playerId)
            onSquadChanged: squad.reload()
        }
    }

    PlayerEditorDialog {
        id: editorDialog

        onSaved: {
            squad.reload();
            if (root.cardOpen)
                card.reload();
        }
        onClosed: {
            if (root.cardOpen)
                card.focusFirst();
            else
                playersTable.forceActiveFocus(Qt.TabFocusReason);
        }
    }
}
