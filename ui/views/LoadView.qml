pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The Load screen (SPEC sections 4 and 7.1, packet P11): the team table with a status per player
// and, beside it, the load of the selected player with its chart. A status is a signal to talk to
// the player, never a diagnosis. Read-only, every role.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate(). playerId: selects that row.
    property var navigationArgs: ({})
    property var selectedPlayer: ({})
    readonly property bool hasSelection: selectedPlayer.playerId !== undefined
    readonly property bool canEnterRpe: AppState.can("EditTraining")
    readonly property bool canEditSquad: AppState.can("EditSquad")
    readonly property alias viewModel: load
    readonly property alias playerPanel: panel

    signal navigateRequested(string key, var args)

    function syncSelection() {
        root.selectedPlayer = load.rowAt(playersTable.currentIndex);
    }

    // The rows changed under the table: the same player stays selected, otherwise the first (the
    // most urgent while the table is sorted by status).
    function restoreSelection() {
        const row = load.rowOf(root.selectedPlayer.playerId ?? 0);
        playersTable.currentIndex = row >= 0 ? row : (playersTable.count > 0 ? 0 : -1);
        root.syncSelection();
    }

    function selectPlayer(playerId) {
        const row = load.rowOf(playerId);
        if (row < 0)
            load.clearFilters();
        playersTable.currentIndex = load.rowOf(playerId);
        root.syncSelection();
    }

    onNavigationArgsChanged: {
        if (navigationArgs.playerId !== undefined)
            root.selectPlayer(navigationArgs.playerId);
    }
    // All views stay loaded, and RPE may have been entered since this one was seen.
    onVisibleChanged: {
        if (!visible)
            return;
        load.reload();
        panel.reload();
    }

    LoadViewModel {
        id: load

        teamId: AppState.teamId
        onPlayersReloaded: {
            root.restoreSelection();
            panel.reload();
        }
        onFiltersChanged: root.restoreSelection()
        onSortChanged: root.restoreSelection()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Load")

            Label {
                text: load.busy ? qsTr("Calculating the load…") : load.asOfText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.body
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            SelectField {
                objectName: "statusFilter"
                textRole: "text"
                valueRole: "value"
                model: load.statusFilterOptions
                value: load.statusFilter
                Accessible.name: qsTr("Status filter")
                onValueActivated: value => load.statusFilter = value
            }

            Label {
                Layout.fillWidth: true
                //: Explains every status of the Load screen.
                text: qsTr("A status compares the load of the last 7 days with the last 28 days. Yellow and red are signals to talk to the player.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
                wrapMode: Text.WordWrap
            }
        }

        // While nobody has a status yet: what the team still needs.
        RowLayout {
            objectName: "teamHint"
            Layout.fillWidth: true
            visible: load.teamHint.length > 0
            spacing: Theme.spacing.m

            Icon {
                name: "info"
                size: Theme.iconSize.s
                color: Theme.info
            }

            Label {
                Layout.fillWidth: true
                text: load.teamHint
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                wrapMode: Text.WordWrap
            }

            SecondaryButton {
                visible: root.canEnterRpe
                text: qsTr("Record a training session")
                onClicked: root.navigateRequested("training", {})
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing.l

            DataTable {
                id: playersTable

                objectName: "loadTable"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 12
                focus: true
                Accessible.name: qsTr("Load of the squad")
                model: load.players
                sortRole: load.sortRole
                sortAscending: load.sortAscending
                columns: [
                    {
                        "role": "name",
                        "title": qsTr("Player"),
                        "width": 0.24
                    },
                    {
                        "role": "acuteText",
                        "title": qsTr("Acute"),
                        "width": 0.1,
                        "align": Text.AlignRight
                    },
                    {
                        "role": "chronicText",
                        "title": qsTr("Chronic"),
                        "width": 0.11,
                        "align": Text.AlignRight
                    },
                    {
                        "role": "ratioText",
                        "title": qsTr("Ratio"),
                        "width": 0.09,
                        "align": Text.AlignRight
                    },
                    {
                        "role": "monotonyText",
                        "title": qsTr("Monotony"),
                        "width": 0.11,
                        "align": Text.AlignRight
                    },
                    {
                        "role": "weekLoadText",
                        "title": qsTr("7 days"),
                        "width": 0.1,
                        "align": Text.AlignRight
                    },
                    {
                        "role": "statusKey",
                        "title": qsTr("Status"),
                        "width": 0.25,
                        "delegate": statusCell
                    }
                ]
                emptyState: playersEmptyState
                onSortRequested: (role, ascending) => load.sortBy(role, ascending)
                onCurrentIndexChanged: root.syncSelection()
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 9
                title: root.hasSelection ? root.selectedPlayer.name : qsTr("Player")

                actions: SecondaryButton {
                    visible: root.hasSelection
                    text: qsTr("Player card")
                    onClicked: root.navigateRequested("squad", {
                        "playerId": root.selectedPlayer.playerId
                    })
                }

                PlayerLoadPanel {
                    id: panel

                    objectName: "loadPlayerPanel"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.hasSelection
                    // The row of the table shows the numbers.
                    showFigures: false
                    playerId: root.selectedPlayer.playerId ?? 0
                    onNavigateRequested: (key, args) => root.navigateRequested(key, args)
                }

                Label {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !root.hasSelection
                    text: qsTr("Select a player in the table to see the load chart.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Component {
        id: statusCell

        DataTableCell {
            id: cell

            HoverHandler {
                id: statusHover
            }

            LoadStatusBadge {
                anchors.verticalCenter: parent.verticalCenter
                statusKey: String(cell.value ?? "")
                text: cell.rowData ? cell.rowData.statusText : ""
            }

            PlainToolTip {
                visible: statusHover.hovered && text.length > 0
                text: cell.rowData ? [cell.rowData.explanation, cell.rowData.hint].filter(part => part.length > 0).join("\n") : ""
                delay: Theme.tooltipDelay
            }
        }
    }

    Component {
        id: playersEmptyState

        EmptyState {
            readonly property bool failed: load.loadError.length > 0
            readonly property bool noPlayers: load.playerCount === 0

            iconName: "load"
            title: failed ? qsTr("The load could not be read") : load.busy ? qsTr("Calculating the load…") : noPlayers ? qsTr("No active players yet") : qsTr("No player has this status")
            description: failed ? load.loadError : load.busy ? "" : noPlayers ? (root.canEditSquad ? qsTr("Add the players of your team, then enter RPE after each training session.") : qsTr("Ask the coach or an assistant to add the players.")) : qsTr("Choose another status to see more players.")
            actionText: failed ? qsTr("Try again") : load.busy ? "" : noPlayers ? (root.canEditSquad ? qsTr("Add players") : "") : qsTr("Show all")
            onActionTriggered: {
                if (failed)
                    load.reload();
                else if (noPlayers)
                    root.navigateRequested("squad", {});
                else
                    load.clearFilters();
            }
        }
    }
}
