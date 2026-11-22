pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The Metrics section (packet P17): pick a custom metric, then the session, the match or the day,
// and enter one value per player.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})
    // What was chosen in a field, waiting for the answer to the question about the changes.
    property var pendingAction: null
    property int lastRow: -1
    readonly property bool canManageMetrics: AppState.can("ManageMetrics")
    readonly property bool canEditCalendar: AppState.can("EditCalendar")
    readonly property bool canEditSquad: AppState.can("EditSquad")
    readonly property bool failed: entry.loadError.length > 0
    readonly property bool hasMetrics: entry.metrics.count > 0
    readonly property bool hasUnsavedChanges: entry.dirty
    readonly property bool showsGrid: !root.failed && entry.hasTarget && entry.count > 0
    readonly property alias viewModel: entry
    readonly property alias grid: valueGrid
    readonly property alias leaveDialog: leaveQuestion

    signal navigateRequested(string key, var args)
    // The answer to askBeforeLeaving(): the changes are saved or dropped, or the user stays.
    signal leaveAccepted
    signal leaveCancelled

    function askBeforeLeaving() {
        root.pendingAction = null;
        leaveQuestion.open();
    }

    function focusContent() {
        if (valueGrid.visible)
            valueGrid.focusFirst();
        else
            metricField.forceActiveFocus(Qt.TabFocusReason);
    }

    function save() {
        if (entry.save()) {
            root.focusAfterSave();
            return true;
        }
        const row = entry.firstErrorRow();
        if (row >= 0)
            valueGrid.focusRow(row);
        return false;
    }

    // Ctrl+S leaves the focus where it is. The button took it away from the grid, so it goes back.
    function focusAfterSave() {
        if (valueGrid.activeFocus)
            return;
        if (root.lastRow >= 0)
            valueGrid.focusRow(root.lastRow);
        else
            valueGrid.focusFirst();
    }

    // Another metric, session, match or day: changes that are not saved ask first.
    function choose(action) {
        if (entry.dirty) {
            root.pendingAction = action;
            leaveQuestion.open();
            return;
        }
        root.run(action);
    }

    function run(action) {
        action();
        root.showPicked();
        root.lastRow = -1;
        // The later call lets the field finish with its list, whichever way the pick came.
        Qt.callLater(root.focusContent);
    }

    // A pick the viewmodel did not take shows what is open again.
    function showPicked() {
        metricField.showValue();
        sessionField.showValue();
        matchField.showValue();
        dateInput.showDate();
    }

    function finishLeaving() {
        const action = root.pendingAction;
        root.pendingAction = null;
        if (action === null) {
            root.leaveAccepted();
            return;
        }
        root.run(action);
    }

    function stay() {
        root.pendingAction = null;
        root.showPicked();
        root.leaveCancelled();
    }

    // Not from the visible change: the order against it is not guaranteed, see Main.qml.
    onNavigationArgsChanged: {
        if (navigationArgs.metricId === undefined)
            return;
        entry.openMetric(navigationArgs.metricId);
        Qt.callLater(root.focusContent);
    }
    // All views stay loaded, and another section may have changed what this one shows.
    onVisibleChanged: {
        if (!visible)
            return;
        entry.reload();
        Qt.callLater(root.focusContent);
    }

    MetricEntryViewModel {
        id: entry

        teamId: AppState.teamId
        onErrorOccurred: message => Toasts.error(message)
    }

    Shortcut {
        id: saveShortcut

        objectName: "metricSaveShortcut"
        sequence: "Ctrl+S"
        enabled: root.visible && entry.canSave && !leaveQuestion.visible
        onActivated: root.save()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Metrics")

            // Not a toast: the focus stays in the grid after a save, and a toast would cover its last
            // rows for 10 s.
            StatusBadge {
                objectName: "metricSaveStateBadge"
                visible: root.showsGrid && !entry.readOnly
                kind: entry.dirty ? StatusBadge.Warn : StatusBadge.Ok
                text: entry.dirty ? qsTr("Not saved") : qsTr("Saved")
            }

            PrimaryButton {
                objectName: "metricSaveButton"
                visible: root.hasMetrics && !entry.readOnly
                enabled: entry.canSave
                text: qsTr("Save")
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Save (%1)").arg(saveShortcut.nativeText)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.save()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.hasMetrics
            spacing: Theme.spacing.l

            FormField {
                id: metricFormField

                Layout.preferredWidth: Theme.fieldWidth
                label: qsTr("Metric")

                SelectField {
                    id: metricField

                    objectName: "metricField"
                    Layout.fillWidth: true
                    model: entry.metrics
                    textRole: "text"
                    valueRole: "value"
                    value: entry.metricId
                    Accessible.name: metricFormField.label
                    onValueActivated: value => {
                        if (value !== entry.metricId)
                            root.choose(() => entry.openMetric(value));
                    }
                }
            }

            FormField {
                id: sessionFormField

                Layout.preferredWidth: Theme.fieldWidth
                visible: entry.scope === "training" && entry.hasTarget
                label: qsTr("Training session")

                SelectField {
                    id: sessionField

                    objectName: "metricSessionField"
                    Layout.fillWidth: true
                    model: entry.sessions
                    textRole: "text"
                    valueRole: "value"
                    value: entry.targetId
                    Accessible.name: sessionFormField.label
                    onValueActivated: value => {
                        if (value !== entry.targetId)
                            root.choose(() => entry.openSession(value));
                    }
                }
            }

            FormField {
                id: matchFormField

                Layout.preferredWidth: Theme.fieldWidth
                visible: entry.scope === "match" && entry.hasTarget
                label: qsTr("Match")

                SelectField {
                    id: matchField

                    objectName: "metricMatchField"
                    Layout.fillWidth: true
                    model: entry.matches
                    textRole: "text"
                    valueRole: "value"
                    value: entry.targetId
                    Accessible.name: matchFormField.label
                    onValueActivated: value => {
                        if (value !== entry.targetId)
                            root.choose(() => entry.openMatch(value));
                    }
                }
            }

            FormField {
                id: dateFormField

                Layout.preferredWidth: Theme.fieldWidth
                visible: entry.scope === "player"
                label: qsTr("Day")
                error: dateInput.effectiveError

                DateField {
                    id: dateInput

                    objectName: "metricDateField"
                    Layout.fillWidth: true
                    date: entry.date
                    Accessible.name: dateFormField.label
                    onDateEdited: date => {
                        if (date !== entry.date)
                            root.choose(() => entry.openDate(date));
                    }
                }
            }

            Label {
                objectName: "metricHint"
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: Theme.spacing.s
                text: entry.metricHint
                textFormat: Text.PlainText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
                wrapMode: Text.Wrap
            }
        }

        MetricValueGrid {
            id: valueGrid

            objectName: "metricValueGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showsGrid
            model: entry
            boolean: entry.valueType === "boolean"
            valueTitle: qsTr("Value")
            readOnly: entry.readOnly
            Accessible.name: qsTr("Values of the metric")
            onValueEdited: (row, text) => entry.setValueText(row, text)
            onCheckToggled: (row, checked) => entry.setChecked(row, checked)
            onValueCleared: row => entry.clearValue(row)
            onCellEntered: row => {
                root.lastRow = row;
                entry.rememberCell(row);
            }
            onCellReverted: row => entry.revertCell(row)
        }

        EmptyState {
            objectName: "metricsLoadFailedState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.failed
            title: qsTr("The metric values could not be read")
            description: entry.loadError
            actionText: qsTr("Try again")
            onActionTriggered: entry.reload()
        }

        EmptyState {
            objectName: "noMetricDefinitionsState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && !root.hasMetrics
            iconName: "metrics"
            title: qsTr("No custom metrics yet")
            description: root.canManageMetrics ? qsTr("Define what your club measures in Settings, then enter the values here.") : qsTr("Ask the coach, the assistant or the analyst to define one in Settings.")
            actionText: root.canManageMetrics ? qsTr("Open the settings") : ""
            onActionTriggered: root.navigateRequested("settings", {})
        }

        EmptyState {
            objectName: "noMetricTargetState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && root.hasMetrics && !entry.hasTarget && entry.scope !== "player"
            iconName: entry.scope === "match" ? "matchday" : "training"
            title: entry.scope === "match" ? qsTr("No matches yet") : qsTr("No sessions yet")
            description: root.canEditCalendar ? qsTr("Add one in Calendar.") : qsTr("Ask the coach or the assistant to add one in Calendar.")
            actionText: root.canEditCalendar ? qsTr("Open the calendar") : ""
            onActionTriggered: root.navigateRequested("calendar", {})
        }

        EmptyState {
            objectName: "noMetricPlayersState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && entry.hasTarget && entry.count === 0
            iconName: "squad"
            title: qsTr("No players yet")
            description: root.canEditSquad ? qsTr("Add players in Squad to enter their values.") : qsTr("Ask the coach or the assistant to add players in Squad.")
            actionText: root.canEditSquad ? qsTr("Open the squad") : ""
            onActionTriggered: root.navigateRequested("squad", {})
        }
    }

    AppDialog {
        id: leaveQuestion

        objectName: "metricLeaveQuestion"
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
            entry.discardChanges();
            root.finishLeaving();
        }
        onRejected: root.stay()

        Label {
            Layout.fillWidth: true
            text: qsTr("The values of this metric have changes that are not saved.")
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }
}
