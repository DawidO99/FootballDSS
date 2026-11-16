pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Matches and training sessions of the team, as a list or as a week, with their editors.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})
    // list or week.
    property string mode: "list"
    // The session the question before a delete is about.
    property double sessionToDelete: 0
    // The match the question before a delete is about.
    property double matchToDelete: 0
    property string matchToDeleteName
    readonly property bool canEdit: AppState.can("EditCalendar")
    readonly property bool weekMode: mode === "week"
    readonly property bool failed: calendar.loadError.length > 0
    readonly property bool dialogOpen: matchDialog.visible || sessionDialog.visible || addChoice.visible || deleteQuestionDialog.visible || deleteMatchQuestionDialog.visible
    readonly property alias viewModel: calendar
    readonly property alias matchEditor: matchDialog
    readonly property alias sessionEditor: sessionDialog
    readonly property alias addChoiceDialog: addChoice
    readonly property alias deleteSessionDialog: deleteQuestionDialog
    readonly property alias deleteMatchDialog: deleteMatchQuestionDialog

    signal navigateRequested(string key, var args)

    function focusContent() {
        if (root.weekMode)
            weekPage.forceActiveFocus(Qt.TabFocusReason);
        else
            itemsTable.forceActiveFocus(Qt.TabFocusReason);
    }

    function addMatch() {
        matchDialog.openForAdding(calendar.defaultNewItemDate());
    }

    function addSession() {
        sessionDialog.openForAdding(calendar.defaultNewItemDate());
    }

    function openItem(kind, itemId) {
        if (kind === "match")
            matchDialog.openForEditing(itemId);
        else
            sessionDialog.openForEditing(itemId);
    }

    // The saved item may be in another week, or far down the list.
    function showSaved(kind, itemId, date) {
        calendar.reload();
        calendar.showWeekOf(date);
        itemsTable.moveSelection(calendar.rowOf(kind, itemId));
    }

    function deleteMatch(matchId, opponentName) {
        if (!calendar.deleteMatch(matchId))
            return;
        matchDialog.close();
        Toasts.show(qsTr("The match against %1 was deleted.").arg(opponentName), qsTr("Undo"), () => calendar.undoDelete());
    }

    function deleteSession(sessionId) {
        if (!calendar.deleteSession(sessionId))
            return;
        sessionDialog.close();
        Toasts.show(qsTr("The session was deleted."), qsTr("Undo"), () => calendar.undoDelete());
    }

    // A saved lineup takes a coach time to set up, so a match that has one asks first (P12 review);
    // the undo toast stays.
    function requestMatchDelete(matchId, opponentName) {
        const saved = calendar.matchHasLineup(matchId);
        if (saved < 0)
            return;
        if (saved === 0) {
            root.deleteMatch(matchId, opponentName);
            return;
        }
        root.matchToDelete = matchId;
        root.matchToDeleteName = opponentName;
        deleteMatchQuestionDialog.open();
    }

    // Attendance takes a coach minutes to enter, so a session that has it asks first (P09a decision 9).
    function requestSessionDelete(sessionId) {
        const count = calendar.attendanceCount(sessionId);
        if (count < 0)
            return;
        if (count === 0) {
            root.deleteSession(sessionId);
            return;
        }
        root.sessionToDelete = sessionId;
        deleteQuestion.text = calendar.deleteConfirmationText(count);
        deleteQuestionDialog.open();
    }

    // All views stay loaded, and another section may have changed what this one shows.
    onVisibleChanged: {
        if (!visible)
            return;
        calendar.reload();
        Qt.callLater(root.focusContent);
    }
    onModeChanged: Qt.callLater(root.focusContent)

    // Reaches here only when the focused control leaves the keys alone, so SegmentedControl and
    // DateField keep their own use of them. A dialog has its own focus chain.
    Keys.onPressed: event => {
        if (!root.weekMode)
            return;
        if (event.key === Qt.Key_Left) {
            calendar.previousWeek();
            event.accepted = true;
        } else if (event.key === Qt.Key_Right) {
            calendar.nextWeek();
            event.accepted = true;
        }
    }

    CalendarViewModel {
        id: calendar

        teamId: AppState.teamId
        onErrorOccurred: message => Toasts.error(message)
    }

    Shortcut {
        id: addShortcut

        objectName: "addShortcut"
        sequence: "Ctrl+N"
        enabled: root.visible && root.canEdit && !root.dialogOpen
        onActivated: addChoice.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Calendar")

            PrimaryButton {
                objectName: "addMatchButton"
                visible: root.canEdit
                text: qsTr("Add match")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add match (%1)").arg(addShortcut.nativeText)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.addMatch()
            }

            SecondaryButton {
                objectName: "addSessionButton"
                visible: root.canEdit
                text: qsTr("Add session")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add session (%1)").arg(addShortcut.nativeText)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.addSession()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            SegmentedControl {
                objectName: "modeControl"
                model: [
                    {
                        "value": "list",
                        "text": qsTr("List")
                    },
                    {
                        "value": "week",
                        "text": qsTr("Week")
                    }
                ]
                currentValue: root.mode
                Accessible.name: qsTr("Calendar view")
                onActivated: value => root.mode = value
            }

            IconButton {
                objectName: "previousWeekButton"
                visible: root.weekMode
                iconName: "chevronLeft"
                text: qsTr("Previous week (←)")
                onClicked: calendar.previousWeek()
            }

            Label {
                objectName: "weekTitle"
                visible: root.weekMode
                text: calendar.weekTitle
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                font.weight: Font.DemiBold
            }

            IconButton {
                objectName: "nextWeekButton"
                visible: root.weekMode
                iconName: "chevronRight"
                text: qsTr("Next week (→)")
                onClicked: calendar.nextWeek()
            }

            SecondaryButton {
                objectName: "todayButton"
                visible: root.weekMode
                enabled: !calendar.showsCurrentWeek
                text: qsTr("Today")
                onClicked: calendar.goToToday()
            }

            Item {
                Layout.fillWidth: true
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.weekMode ? 1 : 0

            DataTable {
                id: itemsTable

                objectName: "calendarTable"
                focus: true
                Accessible.name: qsTr("Calendar")
                model: calendar.items
                sectionRole: "section"
                columns: [
                    {
                        "role": "dateText",
                        "title": qsTr("Date"),
                        "width": 0.24,
                        "sortable": false
                    },
                    {
                        "role": "time",
                        "title": qsTr("Time"),
                        "width": 0.08,
                        "sortable": false
                    },
                    {
                        "role": "kindText",
                        "title": qsTr("Kind"),
                        "width": 0.13,
                        "delegate": kindCell,
                        "sortable": false
                    },
                    {
                        "role": "title",
                        "title": qsTr("Opponent or type"),
                        "width": 0.22,
                        "sortable": false
                    },
                    {
                        "role": "details",
                        "title": qsTr("Details"),
                        "width": 0.2,
                        "sortable": false
                    },
                    {
                        "role": "statusText",
                        "title": qsTr("Status"),
                        "width": 0.13,
                        "delegate": statusCell,
                        "sortable": false
                    }
                ]
                emptyState: itemsEmptyState
                onActivated: index => {
                    const row = calendar.rowAt(index);
                    root.openItem(row.kind, row.itemId);
                }
            }

            FocusScope {
                id: weekPage

                objectName: "weekPage"

                CalendarWeekGrid {
                    objectName: "weekGrid"
                    anchors.fill: parent
                    visible: !weekEmptyState.active
                    model: calendar.week
                    onItemActivated: (kind, itemId) => root.openItem(kind, itemId)
                }

                // A week with nothing in it is normal; a calendar with nothing in it needs a next step.
                Loader {
                    id: weekEmptyState

                    objectName: "weekEmptyState"
                    anchors.fill: parent
                    active: root.failed || calendar.empty
                    sourceComponent: itemsEmptyState
                }
            }
        }
    }

    Component {
        id: kindCell

        DataTableCell {
            id: cell

            Row {
                height: cell.height
                spacing: Theme.spacing.xs

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: cell.rowData && cell.rowData.kind === "match" ? "matchday" : "training"
                    size: Theme.iconSize.s
                    color: Theme.textMuted
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(cell.value ?? "")
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                }
            }
        }
    }

    Component {
        id: statusCell

        DataTableCell {
            id: cell

            // A session has no status.
            MatchStatusBadge {
                anchors.verticalCenter: parent.verticalCenter
                visible: cell.rowData ? cell.rowData.statusKey.length > 0 : false
                statusKey: cell.rowData ? cell.rowData.statusKey : ""
                text: String(cell.value ?? "")
            }
        }
    }

    Component {
        id: itemsEmptyState

        EmptyState {
            objectName: "calendarEmptyState"
            iconName: "calendar"
            title: root.failed ? qsTr("The calendar could not be read") : qsTr("No matches or sessions yet")
            description: root.failed ? calendar.loadError : root.canEdit ? qsTr("Add a match or a training session to start planning.") : qsTr("Ask the coach or an assistant to plan the matches and sessions.")
            actionText: root.failed ? qsTr("Try again") : root.canEdit ? qsTr("Add match") : ""
            onActionTriggered: {
                if (root.failed)
                    calendar.reload();
                else
                    root.addMatch();
            }
        }
    }

    MatchEditorDialog {
        id: matchDialog

        objectName: "matchEditor"
        onSaved: (matchId, date) => root.showSaved("match", matchId, date)
        onDeleteRequested: (matchId, opponentName) => root.requestMatchDelete(matchId, opponentName)
        onClosed: root.focusContent()
    }

    SessionEditorDialog {
        id: sessionDialog

        objectName: "sessionEditor"
        onSaved: (sessionId, date) => root.showSaved("session", sessionId, date)
        onDeleteRequested: sessionId => root.requestSessionDelete(sessionId)
        onOpenSheetRequested: sessionId => {
            sessionDialog.close();
            root.navigateRequested("training", {
                "sessionId": sessionId
            });
        }
        onClosed: root.focusContent()
    }

    AppDialog {
        id: addChoice

        objectName: "addChoiceDialog"
        size: AppDialog.Small
        title: qsTr("What do you want to add?")
        primaryText: ""
        onOpened: addMatchChoice.forceActiveFocus(Qt.TabFocusReason)

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            PrimaryButton {
                id: addMatchChoice

                objectName: "addMatchChoice"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("Match")
                onClicked: {
                    addChoice.close();
                    root.addMatch();
                }
            }

            SecondaryButton {
                objectName: "addSessionChoice"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("Session")
                onClicked: {
                    addChoice.close();
                    root.addSession();
                }
            }
        }
    }

    AppDialog {
        id: deleteQuestionDialog

        objectName: "deleteSessionDialog"
        size: AppDialog.Small
        title: qsTr("Delete session")
        primaryText: ""
        dangerText: qsTr("Delete session")
        onDangerClicked: {
            deleteQuestionDialog.close();
            root.deleteSession(root.sessionToDelete);
        }

        Label {
            id: deleteQuestion

            objectName: "deleteQuestion"
            Layout.fillWidth: true
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.WordWrap
        }
    }

    AppDialog {
        id: deleteMatchQuestionDialog

        objectName: "deleteMatchDialog"
        size: AppDialog.Small
        title: qsTr("Delete match")
        primaryText: ""
        dangerText: qsTr("Delete match")
        onDangerClicked: {
            deleteMatchQuestionDialog.close();
            root.deleteMatch(root.matchToDelete, root.matchToDeleteName);
        }

        Label {
            objectName: "deleteMatchQuestion"
            Layout.fillWidth: true
            text: qsTr("This match has a saved lineup. Delete it together with the lineup?")
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.WordWrap
        }
    }
}
