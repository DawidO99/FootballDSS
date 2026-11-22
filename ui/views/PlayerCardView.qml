pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The card of one player, shown by SquadView. The Back button leads to the list, or to the screen
// the card was opened from (backText names it); Esc is its shortcut.
Item {
    id: root

    // 0: no player.
    property double playerId: 0
    // Where Back leads, e.g. "← Pre-match".
    property string backText: qsTr("← Squad")
    // False while a dialog of SquadView is open: Esc belongs to that dialog then.
    property bool shortcutsEnabled: true
    property var selectedEntry: ({})
    readonly property bool hasSelection: selectedEntry.entryId !== undefined
    readonly property bool canEditPlayer: AppState.can("EditSquad")
    readonly property alias viewModel: card
    readonly property alias availabilityModel: availability
    readonly property alias availabilityEditor: availabilityDialog
    readonly property alias returnEditor: returnDialog
    readonly property alias loadPanel: cardLoadPanel
    readonly property alias matchesModel: matches
    readonly property alias matchesView: matchesTable
    readonly property alias metricsModel: playerMetrics

    signal backRequested
    signal editRequested(double playerId)
    // After a change of availability, which changes the status in the list too.
    signal squadChanged
    // The Load tab leads to the training sheet, the Matches tab to the match report.
    signal navigateRequested(string key, var args)

    function reload() {
        card.reload();
        availability.reload();
        cardLoadPanel.reload();
        matches.reload();
        playerMetrics.reload();
    }

    function openMatchReport(matchId) {
        if (!matchId)
            return;
        root.navigateRequested("matchreport", {
            "matchId": matchId,
            "returnTo": {
                "key": "squad",
                "args": {
                    "playerId": root.playerId
                }
            }
        });
    }

    function focusFirst() {
        overviewPage.scrollTo(0);
        backButton.forceActiveFocus(Qt.TabFocusReason);
    }

    function syncSelection() {
        root.selectedEntry = availability.entries.rowAt(entriesTable.currentIndex);
    }

    function openAddAvailability() {
        availabilityDialog.openForAdding();
    }

    function openEditAvailability() {
        if (root.hasSelection && availability.canEdit)
            availabilityDialog.openForEditing(root.selectedEntry);
    }

    function openMarkReturned() {
        if (!root.hasSelection)
            return;
        availability.clearErrors();
        returnDialog.entryId = root.selectedEntry.entryId;
        returnDialog.minimumDate = root.selectedEntry.fromDate;
        returnDialog.suggestedDate = availability.defaultReturnDate(root.selectedEntry.entryId);
        // The same date as last time changes nothing in the field, so an edit left there would stay.
        returnInput.showDate();
        returnDialog.open();
        returnInput.forceActiveFocus(Qt.TabFocusReason);
    }

    // The table keeps these for itself while it has the focus.
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
            overviewPage.scrollByPage(event.key === Qt.Key_PageDown ? 1 : -1);
            event.accepted = true;
        }
    }

    PlayerCardViewModel {
        id: card

        playerId: root.playerId
    }

    PlayerMetricsViewModel {
        id: playerMetrics

        playerId: root.playerId
    }

    PlayerMatchesModel {
        id: matches

        playerId: root.playerId
    }

    AvailabilityViewModel {
        id: availability

        playerId: root.playerId
        onErrorOccurred: message => Toasts.error(message)
        onEntriesChanged: {
            // The list was read anew: the same entry stays selected.
            entriesTable.currentIndex = availability.entries.rowOf(root.selectedEntry.entryId ?? 0);
            root.syncSelection();
        }
        onAvailabilityChanged: {
            card.reload();
            root.squadChanged();
        }
    }

    Shortcut {
        id: backShortcut

        objectName: "backShortcut"
        sequence: "Esc"
        enabled: root.visible && root.shortcutsEnabled && !availabilityDialog.visible && !returnDialog.visible
        onActivated: root.backRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            SecondaryButton {
                id: backButton

                objectName: "backButton"
                text: root.backText
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back (%1)").arg(backShortcut.nativeText)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.backRequested()
            }

            Label {
                visible: card.number !== undefined
                text: String(card.number ?? "")
                color: Theme.accent
                font.pixelSize: Theme.fontSize.h1
                font.weight: Font.DemiBold
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: card.name
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.h1
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: card.age !== undefined ? qsTr("%1, age %2").arg(card.positionName).arg(card.age) : card.positionName
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    elide: Text.ElideRight
                }
            }

            PlayerStatusBadge {
                visible: card.statusKey.length > 0
                statusKey: card.statusKey
                text: card.statusText
            }

            SecondaryButton {
                visible: root.canEditPlayer
                text: qsTr("Edit player")
                onClicked: root.editRequested(root.playerId)
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: card.loadError.length > 0
            iconName: "user"
            title: qsTr("This player could not be read")
            description: card.loadError
            actionText: qsTr("Try again")
            onActionTriggered: root.reload()
        }

        AppTabs {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: card.loadError.length === 0
            titles: [qsTr("Overview"), qsTr("Load"), qsTr("Matches"), qsTr("Notes")]

            // Long notes and descriptions scroll the page. The cards fill it at least, and stay at the
            // top of a higher one, so the table and its buttons do not move away from the tabs.
            FocusFlickable {
                id: overviewPage

                objectName: "overviewPage"
                contentHeight: Math.max(height, overviewRow.implicitHeight)

                RowLayout {
                    id: overviewRow

                    width: overviewPage.width
                    spacing: Theme.spacing.l

                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: Math.max(overviewPage.height, implicitHeight)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.spacing.l

                        Card {
                            Layout.fillWidth: true
                            title: qsTr("Player data")

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacing.s

                                Repeater {
                                    model: [
                                        {
                                            "key": "position",
                                            "label": qsTr("Position"),
                                            "value": card.positionCode.length > 0 ? card.positionCode + " - " + card.positionName : card.positionName
                                        },
                                        {
                                            "key": "secondaryPositions",
                                            "label": qsTr("Secondary positions"),
                                            "value": card.secondaryPositions
                                        },
                                        {
                                            "key": "birthYear",
                                            "label": qsTr("Birth year"),
                                            "value": String(card.birthYear ?? "")
                                        },
                                        {
                                            "key": "foot",
                                            "label": qsTr("Preferred foot"),
                                            "value": card.footName
                                        },
                                        {
                                            "key": "notes",
                                            "label": qsTr("Notes"),
                                            "value": card.notes
                                        }
                                    ]

                                    delegate: RowLayout {
                                        id: dataRow

                                        required property var modelData

                                        Layout.fillWidth: true
                                        spacing: Theme.spacing.m

                                        Label {
                                            Layout.preferredWidth: Theme.fieldWidth / 2
                                            Layout.alignment: Qt.AlignTop
                                            text: dataRow.modelData.label
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontSize.body
                                            wrapMode: Text.WordWrap
                                        }

                                        // Long notes wrap inside the card, in the middle of a long word too.
                                        Label {
                                            objectName: "playerData_" + dataRow.modelData.key
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 0
                                            text: dataRow.modelData.value.length > 0 ? dataRow.modelData.value : qsTr("Not set")
                                            textFormat: Text.PlainText
                                            color: Theme.text
                                            font.pixelSize: Theme.fontSize.body
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }
                            }

                        }

                        // P17: the latest value and the 28 days of every custom metric of the player.
                        Card {
                            objectName: "playerMetricsCard"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            title: qsTr("Metrics")

                            Repeater {
                                model: playerMetrics

                                delegate: ColumnLayout {
                                    id: metricRow

                                    required property int index
                                    required property string name
                                    required property string latestText
                                    required property string latestDateText
                                    required property string windowText
                                    required property string trend
                                    required property bool improvement
                                    required property string trendText
                                    required property string previousText

                                    objectName: "playerMetric_" + index
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing.xs

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacing.s

                                        Label {
                                            Layout.fillWidth: true
                                            text: metricRow.name
                                            textFormat: Text.PlainText
                                            color: Theme.text
                                            font.pixelSize: Theme.fontSize.body
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            objectName: "playerMetricLatest_" + metricRow.index
                                            text: metricRow.latestText
                                            textFormat: Text.PlainText
                                            color: Theme.text
                                            font.pixelSize: Theme.fontSize.body
                                        }

                                        MetricTrend {
                                            objectName: "playerMetricTrend_" + metricRow.index
                                            trend: metricRow.trend
                                            improvement: metricRow.improvement
                                            text: metricRow.trendText
                                        }
                                    }

                                    Label {
                                        objectName: "playerMetricWindow_" + metricRow.index
                                        Layout.fillWidth: true
                                        text: qsTr("%1 · last %2 · %3").arg(metricRow.windowText).arg(metricRow.latestDateText).arg(metricRow.previousText)
                                        textFormat: Text.PlainText
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontSize.caption
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }

                            EmptyState {
                                objectName: "noPlayerMetricsState"
                                Layout.fillWidth: true
                                visible: playerMetrics.count === 0
                                iconName: "metrics"
                                title: playerMetrics.loadError.length > 0 ? qsTr("The metrics could not be read") : qsTr("No metric values yet")
                                description: playerMetrics.loadError.length > 0 ? playerMetrics.loadError : playerMetrics.canManage ? qsTr("Enter values in the Metrics section.") : qsTr("Ask the coach, the assistant or the analyst to enter them.")
                                actionText: playerMetrics.loadError.length > 0 ? qsTr("Try again") : playerMetrics.canManage ? qsTr("Open metrics") : ""
                                onActionTriggered: {
                                    if (playerMetrics.loadError.length > 0)
                                        playerMetrics.reload();
                                    else
                                        root.navigateRequested("metrics", {});
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }
                    }

                    Card {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: Math.max(overviewPage.height, implicitHeight)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 2
                        title: qsTr("Availability")

                        DataTable {
                            id: entriesTable

                            objectName: "entriesTable"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Accessible.name: qsTr("Availability")
                            model: availability.entries
                            columns: {
                                const columns = [
                                    {
                                        "role": "typeName",
                                        "title": qsTr("Type"),
                                        "width": 0.2,
                                        "sortable": false
                                    },
                                    {
                                        "role": "fromText",
                                        "title": qsTr("From"),
                                        "width": 0.2,
                                        "sortable": false
                                    },
                                    {
                                        "role": "expectedText",
                                        "title": qsTr("Expected"),
                                        "width": 0.2,
                                        "sortable": false
                                    },
                                    {
                                        "role": "actualText",
                                        "title": qsTr("Returned"),
                                        "width": 0.2,
                                        "sortable": false
                                    }
                                ];
                                if (availability.canViewDetails)
                                    columns.push({
                                        "role": "description",
                                        "title": qsTr("Description"),
                                        "width": 0.4,
                                        "sortable": false
                                    });
                                return columns;
                            }
                            emptyState: entriesEmptyState
                            onCurrentIndexChanged: root.syncSelection()
                            onActivated: root.openEditAvailability()
                        }

                        // The table shows one line of a description; here it can be read to the end.
                        Label {
                            objectName: "selectedDescription"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 0
                            visible: availability.canViewDetails && text.length > 0
                            text: root.selectedEntry.description ?? ""
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: Theme.fontSize.body
                            wrapMode: Text.Wrap
                        }

                        Component {
                            id: entriesEmptyState

                            EmptyState {
                                iconName: "calendar"
                                title: availability.loadError.length > 0 ? qsTr("The availability could not be read") : qsTr("No availability entries")
                                description: availability.loadError.length > 0 ? availability.loadError : qsTr("Injuries, illnesses, suspensions and absences of this player will appear here.")
                                actionText: availability.loadError.length > 0 ? qsTr("Try again") : availability.canEdit && !card.archived ? qsTr("Add availability") : ""
                                onActionTriggered: {
                                    if (availability.loadError.length > 0)
                                        availability.reload();
                                    else
                                        root.openAddAvailability();
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: availability.canEdit
                            spacing: Theme.spacing.m

                            PrimaryButton {
                                visible: !card.archived
                                text: qsTr("Add availability")
                                onClicked: root.openAddAvailability()
                            }

                            SecondaryButton {
                                enabled: root.hasSelection
                                text: qsTr("Edit")
                                onClicked: root.openEditAvailability()
                            }

                            SecondaryButton {
                                enabled: root.hasSelection && root.selectedEntry.open
                                text: qsTr("Mark as returned")
                                onClicked: root.openMarkReturned()
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            PlayerLoadPanel {
                id: cardLoadPanel

                objectName: "cardLoadPanel"
                playerId: root.playerId
                onNavigateRequested: (key, args) => root.navigateRequested(key, args)
            }

            // The finished matches of the player, the most recent first (P14); a row opens the report.
            FocusScope {
                id: matchesPage

                objectName: "matchesPage"

                DataTable {
                    id: matchesTable

                    objectName: "playerMatchesTable"
                    anchors.fill: parent
                    Accessible.name: qsTr("Matches")
                    model: matches
                    columns: [
                        {
                            "role": "dateText",
                            "title": qsTr("Date"),
                            "width": 0.14,
                            "sortable": false
                        },
                        {
                            "role": "opponentName",
                            "title": qsTr("Opponent"),
                            "width": 0.22,
                            "sortable": false
                        },
                        {
                            "role": "scoreText",
                            "title": qsTr("Result"),
                            "width": 0.16,
                            "delegate": resultCell,
                            "sortable": false
                        },
                        {
                            "role": "minutes",
                            "title": qsTr("Minutes"),
                            "width": 0.08,
                            "align": Text.AlignRight,
                            "sortable": false
                        },
                        {
                            "role": "goals",
                            "title": qsTr("Goals"),
                            "width": 0.07,
                            "align": Text.AlignRight,
                            "sortable": false
                        },
                        {
                            "role": "assists",
                            "title": qsTr("Assists"),
                            "width": 0.07,
                            "align": Text.AlignRight,
                            "sortable": false
                        },
                        {
                            "role": "yellowCards",
                            "title": qsTr("Yellow"),
                            "width": 0.07,
                            "align": Text.AlignRight,
                            "sortable": false
                        },
                        {
                            "role": "redCards",
                            "title": qsTr("Red"),
                            "width": 0.06,
                            "align": Text.AlignRight,
                            "sortable": false
                        },
                        {
                            "role": "rating",
                            "title": qsTr("Rating"),
                            "width": 0.07,
                            "align": Text.AlignRight,
                            "sortable": false
                        },
                        {
                            "role": "rpe",
                            "title": qsTr("RPE"),
                            "width": 0.06,
                            "align": Text.AlignRight,
                            "sortable": false
                        }
                    ]
                    emptyState: matchesEmptyState
                    onActivated: index => root.openMatchReport(matches.get(index).matchId)
                }

                Component {
                    id: resultCell

                    DataTableCell {
                        id: cell

                        StatusBadge {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: cell.rowData !== undefined && cell.rowData !== null
                            kind: !cell.rowData ? StatusBadge.Neutral : cell.rowData.resultKey === "win" ? StatusBadge.Ok : cell.rowData.resultKey === "loss" ? StatusBadge.Danger : StatusBadge.Neutral
                            text: cell.rowData ? qsTr("%1 %2").arg(cell.rowData.resultWord).arg(cell.rowData.scoreText) : ""
                        }
                    }
                }

                Component {
                    id: matchesEmptyState

                    EmptyState {
                        iconName: "matchday"
                        title: matches.loadError.length > 0 ? qsTr("The matches could not be read") : qsTr("No matches yet")
                        description: matches.loadError.length > 0 ? matches.loadError : qsTr("The finished matches of this player, with minutes and ratings, will appear here. Finish a match in Match Day or fill it in from the Calendar.")
                        actionText: matches.loadError.length > 0 ? qsTr("Try again") : qsTr("Open the calendar")
                        onActionTriggered: {
                            if (matches.loadError.length > 0)
                                matches.reload();
                            else
                                root.navigateRequested("calendar", {});
                        }
                    }
                }
            }

            EmptyState {
                iconName: "edit"
                title: qsTr("Notes")
                description: qsTr("Dated notes about this player will appear here.")
            }
        }
    }

    AvailabilityDialog {
        id: availabilityDialog

        viewModel: availability
    }

    AppDialog {
        id: returnDialog

        property double entryId: 0
        property string minimumDate
        property string suggestedDate

        size: AppDialog.Small
        title: qsTr("Mark as returned")
        primaryText: qsTr("Mark as returned")
        primaryEnabled: returnInput.validationError.length === 0
        onPrimaryClicked: {
            if (availability.markReturned(returnDialog.entryId, returnInput.currentDate))
                returnDialog.close();
        }

        FormField {
            id: returnField

            Layout.fillWidth: true
            label: qsTr("Available again from")
            mandatory: true
            helpText: qsTr("The first day the player is available.")
            error: returnInput.effectiveError

            DateField {
                id: returnInput

                Layout.fillWidth: true
                date: returnDialog.suggestedDate
                minimumDate: returnDialog.minimumDate
                error: availability.actualToError
                Accessible.name: returnField.label
            }
        }
    }
}
