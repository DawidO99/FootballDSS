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
    // The status column holds the longest short status of the language with a day beside it,
    // measured in the font of the badge: fonts differ between machines, so no width in pixels fits
    // them all (P11 CI review). qml_smoke checks it at the minimum window.
    readonly property real statusColumnWidth: statusColumnWidthFor(statusMeasure.fonts, load.statusShortTexts, load.asOfShortSamples)

    signal navigateRequested(string key, var args)

    // The widest word plus the widest day, measured by statusMeasure. `fonts` are the fonts it
    // measures in: passed so that the binding above follows them.
    function statusColumnWidthFor(fonts, words, days) {
        const widest = (texts, width) => texts.reduce((most, text) => Math.max(most, width(text)), 0);
        return widest(words, word => statusMeasure.badgeWidthFor(word)) + widest(days, day => statusMeasure.noteWidthFor(day)) + 2 * playersTable.cellPadding + Theme.measuredTextSlack;
    }

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
        playersTable.currentIndex = load.rowOf(playerId);
        root.syncSelection();
    }

    // The viewmodel selects the player after the next read, also when it is still running.
    onNavigationArgsChanged: {
        if (navigationArgs.playerId !== undefined)
            load.requestPlayer(navigationArgs.playerId);
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
        onPlayerRequested: playerId => root.selectPlayer(playerId)
        onPlayerNotInTable: message => Toasts.info(message)
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
            id: split

            // Table and panel 13 : 7, but the table never narrower than its columns need, so the
            // status column keeps its measured width and the numbers stay readable at the minimum
            // window; the chart beside gets the rest.
            readonly property real tableShare: (width - spacing) * 13 / 20

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing.l

            DataTable {
                id: playersTable

                objectName: "loadTable"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(playersTable.minimumColumnsWidth, split.tableShare)
                Layout.minimumWidth: playersTable.minimumColumnsWidth
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
                        "width": root.statusColumnWidth,
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
                Layout.preferredWidth: Math.max(0, split.width - split.spacing - playersTable.Layout.preferredWidth)
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

    // Not shown: measures the status column in the fonts of the badge.
    LoadStatusBadge {
        id: statusMeasure

        visible: false
    }

    Component {
        id: statusCell

        DataTableCell {
            id: cell

            HoverHandler {
                id: statusHover
            }

            // The short word, and the day when the row describes another day than the table; the
            // full status and its day are in the tooltip and the accessible name.
            LoadStatusBadge {
                objectName: "loadStatus_" + cell.row
                anchors.verticalCenter: parent.verticalCenter
                statusKey: String(cell.value ?? "")
                text: cell.rowData ? cell.rowData.statusShortText : ""
                note: cell.rowData ? cell.rowData.asOfShort : ""
                accessibleName: cell.rowData ? [cell.rowData.statusText, cell.rowData.asOfText].filter(part => part.length > 0).join(", ") : ""
            }

            PlainToolTip {
                visible: statusHover.hovered && text.length > 0
                text: cell.rowData ? [cell.rowData.statusText, cell.rowData.asOfText, cell.rowData.explanation, cell.rowData.hint].filter(part => part.length > 0).join("\n") : ""
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
