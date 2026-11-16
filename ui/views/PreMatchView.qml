pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import FootballDSS

// Pre-match (SPEC sections 4 and 7.2, packet P12): the match, the tactics board with the bench and
// the squad, and the issues of the lineup validator, checked after every change. Mouse, touch and
// keyboard: select a slot (Tab, Enter), then pick a player in the list with Enter; the selection
// moves on to the next free slot. Saving with errors is allowed (a draft); Match Day refuses to start
// with them. Read-only without EditLineup or for a match that is not planned.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate(). matchId: opens that match.
    property var navigationArgs: ({})
    // The match chosen in the field, waiting for the answer about the unsaved changes.
    property double pendingMatchId: 0
    // A slot waiting for the slot to swap with; -1: none.
    property int swapFrom: -1
    readonly property bool canEditCalendar: AppState.can("EditCalendar")
    readonly property bool canEditSquad: AppState.can("EditSquad")
    readonly property bool canEdit: !preMatch.readOnly
    readonly property bool failed: preMatch.loadError.length > 0
    readonly property bool hasUnsavedChanges: preMatch.dirty
    readonly property var selectedRow: preMatch.selectedSlot >= 0 ? preMatch.board.get(preMatch.selectedSlot) : ({})
    readonly property alias viewModel: preMatch
    readonly property alias pitch: pitch
    readonly property alias bench: benchList
    readonly property alias playerList: playerList
    readonly property alias issuesPanel: issuesPanel
    readonly property alias leaveDialog: leaveQuestion
    readonly property alias templatesDialog: templatesDialog

    signal navigateRequested(string key, var args)
    // The answer to askBeforeLeaving(): the changes are saved or dropped, or the user stays.
    signal leaveAccepted
    signal leaveCancelled

    function askBeforeLeaving() {
        root.pendingMatchId = 0;
        leaveQuestion.open();
    }

    function save() {
        return preMatch.save();
    }

    function chooseMatch(matchId) {
        if (matchId === preMatch.matchId)
            return;
        if (preMatch.dirty) {
            root.pendingMatchId = matchId;
            leaveQuestion.open();
            return;
        }
        root.openChosen(matchId);
    }

    // A match that cannot be read leaves the open one on the screen, so the field shows that one again.
    function openChosen(matchId) {
        root.swapFrom = -1;
        preMatch.openMatch(matchId);
        matchField.showValue();
    }

    function finishLeaving() {
        const matchId = root.pendingMatchId;
        root.pendingMatchId = 0;
        if (matchId === 0) {
            root.leaveAccepted();
            return;
        }
        root.openChosen(matchId);
    }

    function stay() {
        root.pendingMatchId = 0;
        matchField.showValue();
        root.leaveCancelled();
    }

    // A token was clicked or got Enter: select it, or finish a swap started with the swap button.
    function activateSlot(slot) {
        if (root.swapFrom >= 0 && root.swapFrom !== slot && root.canEdit) {
            preMatch.swapSlots(root.swapFrom, slot);
            root.swapFrom = -1;
        }
        preMatch.selectSlot(slot);
    }

    // Not from the visible change: the order against it is not guaranteed, see Main.qml.
    onNavigationArgsChanged: {
        if (navigationArgs.matchId !== undefined)
            root.chooseMatch(navigationArgs.matchId);
    }
    // All views stay loaded, and the squad, the calendar or the load may have changed meanwhile.
    onVisibleChanged: {
        if (visible)
            preMatch.reload();
    }

    PreMatchViewModel {
        id: preMatch

        teamId: AppState.teamId
        yellowCardThreshold: AppSettings.yellowCardThreshold
        onErrorOccurred: message => Toasts.error(message)
        onNotice: message => Toasts.info(message)
        onSaved: message => Toasts.info(message)
        onTemplateNameTaken: name => {
            replaceQuestion.templateName = name;
            replaceQuestion.open();
        }
        onTemplateDeleted: message => Toasts.show(message, qsTr("Undo"), () => preMatch.undoDeleteTemplate())
        onSlotFocusRequested: slot => pitch.focusSlot(slot)
        onBenchFocusRequested: index => benchList.focusRow(index)
    }

    Shortcut {
        id: saveShortcut

        objectName: "saveShortcut"
        sequence: "Ctrl+S"
        enabled: root.visible && root.canEdit && !leaveQuestion.visible && !templatesDialog.visible
        onActivated: root.save()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Pre-match")

            StatusBadge {
                objectName: "saveStateBadge"
                visible: preMatch.hasMatch && root.canEdit
                kind: preMatch.dirty ? StatusBadge.Warn : preMatch.hasSavedLineup ? StatusBadge.Ok : StatusBadge.Neutral
                text: preMatch.dirty ? qsTr("Unsaved changes") : preMatch.hasSavedLineup ? qsTr("Saved") : qsTr("Not saved yet")
            }

            SelectField {
                id: matchField

                objectName: "matchField"
                visible: preMatch.hasPlannedMatches || preMatch.hasMatch
                model: preMatch.matches
                textRole: "text"
                valueRole: "value"
                value: preMatch.matchId
                Accessible.name: qsTr("Match")
                onValueActivated: value => root.chooseMatch(value)
            }

            PrimaryButton {
                objectName: "saveButton"
                visible: preMatch.hasMatch && root.canEdit
                text: qsTr("Save")
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Save (%1)").arg(saveShortcut.nativeText)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.save()
            }
        }

        // The match: opponent, date, venue, competition and the last result against them.
        RowLayout {
            objectName: "matchHeader"
            Layout.fillWidth: true
            visible: preMatch.hasMatch
            spacing: Theme.spacing.m

            Label {
                text: preMatch.opponentName
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.title
                font.weight: Font.DemiBold
            }

            Label {
                text: preMatch.matchDetailsText
                textFormat: Text.PlainText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.body
            }

            Label {
                objectName: "lastMeeting"
                Layout.fillWidth: true
                text: preMatch.lastMeetingText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.body
                elide: Text.ElideRight
            }
        }

        RowLayout {
            objectName: "readOnlyNote"
            Layout.fillWidth: true
            visible: preMatch.hasMatch && preMatch.readOnlyText.length > 0
            spacing: Theme.spacing.s

            Icon {
                name: "info"
                size: Theme.iconSize.s
                color: Theme.info
            }

            Label {
                Layout.fillWidth: true
                text: preMatch.readOnlyText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            objectName: "boardToolbar"
            Layout.fillWidth: true
            visible: preMatch.hasMatch && preMatch.playerCount > 0
            spacing: Theme.spacing.m

            Label {
                text: qsTr("Formation")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.body
            }

            SelectField {
                id: formationField

                objectName: "formationField"
                enabled: root.canEdit
                model: preMatch.formationOptions
                textRole: "text"
                valueRole: "value"
                value: preMatch.formation
                Accessible.name: qsTr("Formation")
                onValueActivated: value => preMatch.formation = value
            }

            Item {
                Layout.fillWidth: true
            }

            SecondaryButton {
                objectName: "templatesButton"
                text: qsTr("Templates")
                onClicked: templatesDialog.open()
            }

            SecondaryButton {
                objectName: "importButton"
                visible: root.canEdit
                text: qsTr("Import file")
                onClicked: importDialog.open()
            }

            SecondaryButton {
                objectName: "exportButton"
                text: qsTr("Export file")
                onClicked: exportDialog.open()
            }

            SecondaryButton {
                objectName: "clearButton"
                visible: root.canEdit
                enabled: preMatch.startingCount > 0 || preMatch.benchCount > 0
                text: qsTr("Clear")
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Take every player out of the lineup. The formation stays.")
                ToolTip.delay: Theme.tooltipDelay
                onClicked: {
                    root.swapFrom = -1;
                    preMatch.clearLineup();
                }
            }
        }

        RowLayout {
            objectName: "boardArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: preMatch.hasMatch && preMatch.playerCount > 0 && !root.failed
            spacing: Theme.spacing.m

            TacticsPitch {
                id: pitch

                objectName: "tacticsPitch"
                Layout.fillHeight: true
                Layout.preferredWidth: pitch.height * Theme.pitch.aspect
                model: preMatch.board
                zones: preMatch.zones
                interactive: root.canEdit
                onSlotActivated: slot => root.activateSlot(slot)
                onPlayerDropped: (slot, playerId) => preMatch.assignToSlot(slot, playerId)
                onSlotsSwapped: (from, to) => preMatch.swapSlots(from, to)
                onSlotMoved: (slot, x, y) => preMatch.moveSlotTo(slot, x, y)
                onRemoveRequested: slot => {
                    const playerId = preMatch.board.get(slot).playerId;
                    if (playerId)
                        preMatch.removePlayer(playerId);
                }
            }

            ColumnLayout {
                Layout.preferredWidth: Theme.benchColumnWidth
                Layout.maximumWidth: Theme.benchColumnWidth
                Layout.fillHeight: true
                spacing: Theme.spacing.m

                // The selected slot: its position (the keyboard path of a drag to another zone), the
                // swap, the bench and taking the player out.
                ColumnLayout {
                    objectName: "selectedSlotPanel"
                    Layout.fillWidth: true
                    visible: preMatch.selectedSlot >= 0
                    spacing: Theme.spacing.s

                    Label {
                        Layout.fillWidth: true
                        text: root.selectedRow.fullName ? qsTr("Selected: %1").arg(root.selectedRow.fullName) : qsTr("Selected: free slot")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        font.weight: Font.DemiBold
                    }

                    SelectField {
                        id: positionField

                        objectName: "positionField"
                        Layout.fillWidth: true
                        enabled: root.canEdit
                        model: preMatch.positionOptions
                        textRole: "text"
                        valueRole: "value"
                        value: preMatch.selectedPosition
                        Accessible.name: qsTr("Position")
                        onValueActivated: value => preMatch.setSlotPosition(preMatch.selectedSlot, value)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.canEdit
                        spacing: Theme.spacing.xs

                        IconButton {
                            objectName: "swapButton"
                            touch: true
                            iconName: "swap"
                            iconColor: root.swapFrom >= 0 ? Theme.accent : Theme.text
                            text: root.swapFrom >= 0 ? qsTr("Now choose the slot to swap with") : qsTr("Swap with another slot")
                            onClicked: root.swapFrom = root.swapFrom >= 0 ? -1 : preMatch.selectedSlot
                        }

                        IconButton {
                            objectName: "toBenchButton"
                            touch: true
                            enabled: preMatch.selectedPlayerId > 0
                            iconName: "chevronRight"
                            text: qsTr("Move to the bench")
                            onClicked: preMatch.moveSlotToBench(preMatch.selectedSlot)
                        }

                        IconButton {
                            objectName: "removeButton"
                            touch: true
                            enabled: preMatch.selectedPlayerId > 0
                            iconName: "close"
                            text: qsTr("Take out of the lineup (Del)")
                            onClicked: preMatch.removePlayer(preMatch.selectedPlayerId)
                        }
                    }
                }

                BenchList {
                    id: benchList

                    objectName: "benchList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: preMatch.bench
                    maxCount: preMatch.maxBench
                    interactive: root.canEdit
                    onPlayerDropped: playerId => preMatch.addToBench(playerId)
                    onSlotDropped: slot => preMatch.moveSlotToBench(slot)
                    onPickRequested: playerId => preMatch.pickPlayer(playerId)
                    onRemoveRequested: playerId => preMatch.removePlayer(playerId)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacing.m

                Label {
                    Layout.fillWidth: true
                    text: root.canEdit ? (preMatch.selectedSlot >= 0 ? qsTr("Pick a player for the selected slot: click or Enter.") : qsTr("Select a slot on the board, then pick a player.")) : qsTr("Squad")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                    elide: Text.ElideRight
                }

                LineupPlayerList {
                    id: playerList

                    objectName: "lineupPlayerList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: preMatch.players
                    interactive: root.canEdit
                    onPickRequested: playerId => preMatch.pickPlayer(playerId)
                    onBenchRequested: playerId => preMatch.addToBench(playerId)
                    onRemoveRequested: playerId => preMatch.removePlayer(playerId)
                }

                LineupIssuesPanel {
                    id: issuesPanel

                    objectName: "lineupIssuesPanel"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.issuesPanelHeight
                    model: preMatch.issues
                    summary: preMatch.issuesSummary
                    errorCount: preMatch.errorCount
                    loadPending: preMatch.loadPending
                    onIssueActivated: row => preMatch.activateIssue(row)
                }
            }
        }

        EmptyState {
            objectName: "loadErrorState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.failed
            iconName: "warn"
            title: qsTr("The match could not be read")
            description: preMatch.loadError
            actionText: qsTr("Try again")
            onActionTriggered: preMatch.reload()
        }

        EmptyState {
            objectName: "noMatchState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && !preMatch.hasMatch
            iconName: "calendar"
            title: qsTr("No planned match")
            description: root.canEditCalendar ? qsTr("Add the next match in the Calendar to set up its lineup.") : qsTr("Ask the coach or the assistant to add the next match in the Calendar.")
            actionText: root.canEditCalendar ? qsTr("Open the calendar") : ""
            onActionTriggered: root.navigateRequested("calendar", {})
        }

        EmptyState {
            objectName: "noPlayersState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && preMatch.hasMatch && preMatch.playerCount === 0
            iconName: "squad"
            title: qsTr("No players yet")
            description: root.canEditSquad ? qsTr("Add players in Squad to set up the lineup.") : qsTr("Ask the coach or the assistant to add players in Squad.")
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
            preMatch.discardChanges();
            root.finishLeaving();
        }
        onRejected: root.stay()

        Label {
            Layout.fillWidth: true
            text: qsTr("The lineup of this match has changes that are not saved.")
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }

    AppDialog {
        id: templatesDialog

        objectName: "templatesDialog"
        title: qsTr("Templates")
        primaryText: qsTr("Save as template")
        primaryEnabled: preMatch.hasMatch && root.canEdit && templateNameField.text.trim().length > 0
        secondaryText: qsTr("Close")
        onOpened: templateNameField.text = ""
        onPrimaryClicked: {
            if (preMatch.saveTemplate(templateNameField.text, false))
                templateNameField.text = "";
        }

        FormField {
            Layout.fillWidth: true
            visible: root.canEdit
            label: qsTr("Save the current lineup as")

            TextInputField {
                id: templateNameField

                objectName: "templateNameField"
                Layout.fillWidth: true
                maximumLength: 50
                placeholderText: qsTr("Template name")
                Accessible.name: qsTr("Template name")
            }
        }

        FormField {
            Layout.fillWidth: true
            label: qsTr("Saved templates")
            helpText: preMatch.hasTemplates ? "" : qsTr("No templates yet.")

            SelectField {
                id: templateField

                objectName: "templateField"
                Layout.fillWidth: true
                visible: preMatch.hasTemplates
                model: preMatch.templates
                textRole: "text"
                valueRole: "value"
                Accessible.name: qsTr("Saved templates")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: preMatch.hasTemplates && root.canEdit
            spacing: Theme.spacing.m

            SecondaryButton {
                objectName: "loadTemplateButton"
                text: qsTr("Load into the board")
                enabled: templateField.currentIndex >= 0
                onClicked: {
                    if (preMatch.loadTemplate(templateField.currentValue))
                        templatesDialog.close();
                }
            }

            DangerButton {
                objectName: "deleteTemplateButton"
                text: qsTr("Delete")
                enabled: templateField.currentIndex >= 0
                onClicked: preMatch.deleteTemplate(templateField.currentValue)
            }
        }
    }

    AppDialog {
        id: replaceQuestion

        property string templateName

        objectName: "replaceTemplateQuestion"
        size: AppDialog.Small
        title: qsTr("Replace the template?")
        primaryText: qsTr("Replace")
        secondaryText: qsTr("Cancel")
        onPrimaryClicked: {
            replaceQuestion.close();
            if (preMatch.saveTemplate(replaceQuestion.templateName, true))
                templateNameField.text = "";
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("A template named %1 already exists. Replace it with the current lineup?").arg(replaceQuestion.templateName)
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }

    FileDialog {
        id: exportDialog

        title: qsTr("Export the lineup")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "tactic.json"
        nameFilters: [qsTr("Tactics files (*.tactic.json)")]
        onAccepted: preMatch.exportTactic(selectedFile)
    }

    FileDialog {
        id: importDialog

        title: qsTr("Import a lineup")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Tactics files (*.tactic.json *.json)")]
        onAccepted: preMatch.importTactic(selectedFile)
    }
}
