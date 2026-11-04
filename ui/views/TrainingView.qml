pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The training sheet: attendance and RPE of the whole squad, for the keyboard.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})
    // The session chosen in the field, waiting for the answer to the question about the changes.
    property double pendingSessionId: 0
    // The session when the list of the field opened.
    property double sessionAtPopupOpen: 0
    property int lastRow: -1
    property int lastColumn: TrainingSessionViewModel.RpeColumn
    readonly property bool canEditCalendar: AppState.can("EditCalendar")
    readonly property bool canEditSquad: AppState.can("EditSquad")
    readonly property bool failed: sheet.loadError.length > 0
    readonly property bool hasUnsavedChanges: sheet.dirty
    readonly property alias viewModel: sheet
    readonly property alias grid: attendanceGrid
    readonly property alias leaveDialog: leaveQuestion

    signal navigateRequested(string key, var args)
    // The answer to askBeforeLeaving(): the changes are saved or dropped, or the user stays.
    signal leaveAccepted
    signal leaveCancelled

    function askBeforeLeaving() {
        root.pendingSessionId = 0;
        leaveQuestion.open();
    }

    function focusContent() {
        if (attendanceGrid.visible)
            attendanceGrid.focusFirstEditable();
    }

    function save() {
        if (sheet.save()) {
            root.focusAfterSave();
            return true;
        }
        const row = sheet.firstErrorRow();
        if (row >= 0)
            attendanceGrid.focusCell(row, TrainingSessionViewModel.MinutesColumn);
        return false;
    }

    // Ctrl+S leaves the focus where it is. The button took it away from the grid, so it goes back.
    function focusAfterSave() {
        if (attendanceGrid.activeFocus)
            return;
        if (root.lastRow >= 0)
            attendanceGrid.focusCell(root.lastRow, root.lastColumn);
        else
            attendanceGrid.focusFirstEditable();
    }

    function chooseSession(sessionId) {
        if (sessionId === sheet.sessionId)
            return;
        if (sheet.dirty) {
            root.pendingSessionId = sessionId;
            leaveQuestion.open();
            return;
        }
        sheet.openSession(sessionId);
    }

    function finishLeaving() {
        const sessionId = root.pendingSessionId;
        root.pendingSessionId = 0;
        if (sessionId === 0) {
            root.leaveAccepted();
            return;
        }
        sheet.openSession(sessionId);
        Qt.callLater(root.focusContent);
    }

    function stay() {
        root.pendingSessionId = 0;
        sessionField.showValue();
        root.leaveCancelled();
    }

    function focusAfterPopup() {
        if (sheet.sessionId !== root.sessionAtPopupOpen && !leaveQuestion.visible)
            root.focusContent();
    }

    // Not from the visible change: the order against it is not guaranteed, see Main.qml.
    onNavigationArgsChanged: {
        if (navigationArgs.sessionId === undefined)
            return;
        sheet.openSession(navigationArgs.sessionId);
        Qt.callLater(root.focusContent);
    }
    // All views stay loaded, and another section may have changed what this one shows.
    onVisibleChanged: {
        if (!visible)
            return;
        sheet.reload();
        Qt.callLater(root.focusContent);
    }

    TrainingSessionViewModel {
        id: sheet

        teamId: AppState.teamId
        onErrorOccurred: message => Toasts.error(message)
    }

    Shortcut {
        id: saveShortcut

        objectName: "saveShortcut"
        sequence: "Ctrl+S"
        enabled: root.visible && sheet.canSave && !leaveQuestion.visible
        onActivated: root.save()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Training")

            // Not a toast: the focus stays in the grid after a save, and a toast would cover its last
            // rows for 10 s.
            StatusBadge {
                objectName: "savedBadge"
                visible: sheet.hasSessions && sheet.count > 0 && !sheet.readOnly && !sheet.canSave
                kind: StatusBadge.Ok
                text: qsTr("Saved")
            }

            SelectField {
                id: sessionField

                objectName: "sessionField"
                visible: sheet.hasSessions
                model: sheet.sessions
                textRole: "text"
                valueRole: "value"
                value: sheet.sessionId
                Accessible.name: qsTr("Training session")
                onValueActivated: value => root.chooseSession(value)

                Connections {
                    target: sessionField.popup

                    function onOpened() {
                        root.sessionAtPopupOpen = sheet.sessionId;
                    }

                    function onClosed() {
                        Qt.callLater(root.focusAfterPopup);
                    }
                }
            }

            SecondaryButton {
                objectName: "markAllPresentButton"
                visible: sheet.hasSessions && !sheet.readOnly
                enabled: sheet.count > 0
                text: qsTr("Mark all present")
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Everyone present, except players with an injury, illness or absence on the day of the session. Those can be changed one by one.")
                ToolTip.delay: Theme.tooltipDelay
                onClicked: sheet.markAllPresent()
            }

            PrimaryButton {
                objectName: "saveButton"
                visible: sheet.hasSessions && !sheet.readOnly
                enabled: sheet.canSave
                text: qsTr("Save")
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Save (%1)").arg(saveShortcut.nativeText)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.save()
            }
        }

        AttendanceGrid {
            id: attendanceGrid

            objectName: "attendanceGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && sheet.hasSessions && sheet.count > 0
            model: sheet
            statusOptions: sheet.statusOptions
            readOnly: sheet.readOnly
            Accessible.name: qsTr("Attendance and RPE")
            onStatusPicked: (row, status) => sheet.setStatus(row, status)
            onDigitEntered: (row, digit) => sheet.enterRpeDigit(row, digit)
            onRpeCleared: row => sheet.clearRpe(row)
            onMinutesEdited: (row, text) => sheet.setMinutesText(row, text)
            onCellEntered: (row, column) => {
                root.lastRow = row;
                root.lastColumn = column;
                sheet.rememberCell(row, column);
            }
            onCellReverted: (row, column) => sheet.revertCell(row, column)
        }

        EmptyState {
            objectName: "loadFailedState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.failed
            title: qsTr("The training sheet could not be read")
            description: sheet.loadError
            actionText: qsTr("Try again")
            onActionTriggered: sheet.reload()
        }

        EmptyState {
            objectName: "noSessionsState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && !sheet.hasSessions
            iconName: "training"
            title: qsTr("No sessions yet")
            description: root.canEditCalendar ? qsTr("Add one in Calendar.") : qsTr("Ask the coach or the assistant to add one in Calendar.")
            actionText: root.canEditCalendar ? qsTr("Open the calendar") : ""
            onActionTriggered: root.navigateRequested("calendar", {})
        }

        EmptyState {
            objectName: "noPlayersState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && sheet.hasSessions && sheet.count === 0
            iconName: "squad"
            title: qsTr("No players yet")
            description: root.canEditSquad ? qsTr("Add players in Squad to record their attendance.") : qsTr("Ask the coach or the assistant to add players in Squad.")
            actionText: root.canEditSquad ? qsTr("Open the squad") : ""
            onActionTriggered: root.navigateRequested("squad", {})
        }
    }

    AppDialog {
        id: leaveQuestion

        objectName: "leaveQuestion"
        size: AppDialog.Small
        title: qsTr("Save the changes?")
        primaryText: qsTr("Save")
        secondaryText: qsTr("Stay")
        dangerText: qsTr("Discard changes")
        onPrimaryClicked: {
            leaveQuestion.close();
            if (root.save())
                root.finishLeaving();
            else
                root.stay();
        }
        onDangerClicked: {
            leaveQuestion.close();
            sheet.discardChanges();
            root.finishLeaving();
        }
        onRejected: root.stay()

        Label {
            Layout.fillWidth: true
            text: qsTr("The attendance of this session has changes that are not saved.")
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }
}
