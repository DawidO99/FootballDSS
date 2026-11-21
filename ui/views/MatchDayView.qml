pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Match Day (SPEC section 4, packet P13): the score and the clock, large buttons for the events,
// the players as tiles and the timeline. Every action is a tap or a click: an action button first,
// then the player tiles it asks for; letters are only accelerators of the visible buttons. The clock
// is derived from the stored steps, so a closed app resumes the running match. Leaving a live match
// asks first (Main.qml guard). Read-only without RunMatchDay. Live tactics (P13b): "Tactics" in
// place of the tiles shows the team on the board; a change of shape is a draft until Confirm, a
// bench player dropped on a token is a substitution waiting for Save.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate(). matchId: opens that match.
    property var navigationArgs: ({})
    // The visibility of the window before the full screen of Match Day; -1: not in full screen.
    property int windowVisibilityBefore: -1
    readonly property bool canEditCalendar: AppState.can("EditCalendar")
    readonly property bool failed: matchDay.loadError.length > 0
    readonly property bool started: matchDay.statusKey === "live" || matchDay.statusKey === "finished"
    readonly property bool choosing: matchDay.pendingAction.length > 0
    // The pending action is saved with the button of the bar (and Enter in its minute field).
    readonly property bool confirmable: matchDay.pendingAction === "opponentGoal" || matchDay.pendingAction === "subConfirm" || matchDay.pendingAction === "tactic"
    // players (the tiles) or tactics (the board, P13b).
    property string boardMode: "players"
    // A slot waiting for the slot to swap with on the board (keyboard path of P12); -1: none.
    property int swapFrom: -1
    readonly property bool dialogOpen: startDialog.visible || leaveQuestion.visible || noteDialog.visible || tooFewDialog.visible || setClockDialog.visible
    // The single-letter accelerators and Ctrl+Z stay off while a dialog (of this view or any other
    // popup, e.g. Change PIN) holds the focus, and while a text field does: there the letters are
    // typed and Ctrl+Z undoes the typing (P13 review).
    readonly property bool shortcutsFree: root.visible && !root.dialogOpen && !root.focusInPopup(root.Window.activeFocusItem) && !root.isTextInput(root.Window.activeFocusItem)
    readonly property bool fullScreen: root.windowVisibilityBefore >= 0
    // A live match: leaving asks first (Main.qml), as long as this section is the one shown.
    readonly property bool hasUnsavedChanges: matchDay.guardLeaving && root.visible
    readonly property alias viewModel: matchDay
    readonly property alias startMatchDialog: startDialog
    readonly property alias leaveDialog: leaveQuestion
    readonly property alias noteEditor: noteDialog
    readonly property alias tooFewPlayersDialog: tooFewDialog
    readonly property alias setClockEditor: setClockDialog

    signal navigateRequested(string key, var args)
    // The answer to askBeforeLeaving(): leave the live match, or stay.
    signal leaveAccepted
    signal leaveCancelled

    function isTextInput(item) {
        return item !== null && item !== undefined && "cursorPosition" in item && "selectAll" in item;
    }

    function focusInPopup(item) {
        const overlay = root.Overlay.overlay;
        for (let current = item; current; current = current.parent) {
            if (current === overlay)
                return true;
        }
        return false;
    }

    function askBeforeLeaving() {
        leaveQuestion.open();
    }

    function toggleFullScreen() {
        const window = root.Window.window;
        if (!window)
            return;
        if (root.fullScreen) {
            root.leaveFullScreen();
            return;
        }
        root.windowVisibilityBefore = window.visibility;
        window.showFullScreen();
    }

    function leaveFullScreen() {
        const window = root.Window.window;
        const before = root.windowVisibilityBefore;
        root.windowVisibilityBefore = -1;
        if (!window || before < 0)
            return;
        if (before === Window.Maximized)
            window.showMaximized();
        else
            window.showNormal();
    }

    function addNote() {
        if (matchDay.canAddNote)
            noteDialog.openForAdding(matchDay.defaultNoteMinute());
    }

    function deleteNote(noteId) {
        const text = matchDay.deleteNote(noteId);
        if (text.length > 0)
            Toasts.show(text, qsTr("Undo"), () => matchDay.restoreDeletedNote());
    }

    // Every end of the match goes one way (service); below 7 players it asks first.
    function endMatch() {
        if (matchDay.tooFewPlayers)
            tooFewDialog.open();
        else
            matchDay.endMatch();
    }

    function setClock(text) {
        const toast = matchDay.setClock(text);
        if (toast.length === 0)
            return false;
        Toasts.show(toast, qsTr("Undo"), () => matchDay.undoSetClock());
        return true;
    }

    function undo() {
        const text = matchDay.undo();
        if (text.length > 0)
            Toasts.info(text);
    }

    // A token was clicked or got Enter: the player of an action waiting for one on the pitch,
    // otherwise finish a swap started with the swap button, or select it.
    function activateLiveSlot(slot) {
        const waiting = ["scorer", "assist", "yellow", "red", "subOff"];
        if (waiting.indexOf(matchDay.pendingAction) >= 0) {
            const playerId = matchDay.board.get(slot).playerId;
            if (playerId)
                matchDay.choosePlayer(playerId);
            return;
        }
        if (root.swapFrom >= 0 && root.swapFrom !== slot) {
            matchDay.swapSlots(root.swapFrom, slot);
            root.swapFrom = -1;
        }
        matchDay.selectSlot(slot);
    }

    function cancelOrExit() {
        root.swapFrom = -1;
        if (root.choosing)
            matchDay.cancelAction();
        else if (root.fullScreen)
            root.leaveFullScreen();
    }

    // Not from the visible change: the order against it is not guaranteed, see Main.qml.
    onNavigationArgsChanged: {
        if (navigationArgs.matchId !== undefined)
            matchDay.openMatch(navigationArgs.matchId);
    }
    // All views stay loaded, and the lineup or the calendar may have changed meanwhile.
    onVisibleChanged: {
        if (visible) {
            if (navigationArgs.matchId === undefined)
                matchDay.reload();
        } else {
            matchDay.cancelAction();
            root.leaveFullScreen();
        }
    }
    // Views are built for every login: a live match of the team opens Match Day at once. Later than
    // the move to the dashboard that every login makes (Main.qml).
    Component.onCompleted: Qt.callLater(() => {
        matchDay.openDefaultMatch();
        if (matchDay.statusKey === "live")
            root.navigateRequested("matchday", {});
    })
    Component.onDestruction: root.leaveFullScreen()

    MatchDayViewModel {
        id: matchDay

        teamId: AppState.teamId
        yellowCardThreshold: AppSettings.yellowCardThreshold
        onErrorOccurred: message => Toasts.error(message)
        onNotice: message => Toasts.info(message)
        onTooFewPlayersReached: tooFewDialog.open()
    }

    Shortcut {
        id: goalShortcut

        objectName: "goalShortcut"
        sequence: "G"
        enabled: root.shortcutsFree && matchDay.canRecordEvents
        onActivated: matchDay.beginAction("goal")
    }

    Shortcut {
        id: opponentGoalShortcut

        objectName: "opponentGoalShortcut"
        sequence: "O"
        enabled: root.shortcutsFree && matchDay.canRecordEvents
        onActivated: matchDay.beginAction("opponentGoal")
    }

    Shortcut {
        id: noteShortcut

        objectName: "noteShortcut"
        sequence: "N"
        enabled: root.shortcutsFree && matchDay.canAddNote
        onActivated: root.addNote()
    }

    Shortcut {
        id: yellowShortcut

        objectName: "yellowShortcut"
        sequence: "Y"
        enabled: root.shortcutsFree && matchDay.canRecordEvents
        onActivated: matchDay.beginAction("yellow")
    }

    Shortcut {
        id: redShortcut

        objectName: "redShortcut"
        sequence: "R"
        enabled: root.shortcutsFree && matchDay.canRecordEvents
        onActivated: matchDay.beginAction("red")
    }

    Shortcut {
        id: subShortcut

        objectName: "subShortcut"
        sequence: "S"
        enabled: root.shortcutsFree && matchDay.canSubstitute
        onActivated: matchDay.beginAction("sub")
    }

    Shortcut {
        id: undoShortcut

        objectName: "undoShortcut"
        sequence: "Ctrl+Z"
        enabled: root.shortcutsFree && matchDay.canUndo
        onActivated: root.undo()
    }

    Shortcut {
        id: fullScreenShortcut

        objectName: "fullScreenShortcut"
        sequence: "F11"
        enabled: root.shortcutsFree && matchDay.hasMatch
        onActivated: root.toggleFullScreen()
    }

    Shortcut {
        id: cancelShortcut

        objectName: "cancelShortcut"
        sequence: "Esc"
        enabled: root.shortcutsFree && (root.choosing || root.fullScreen)
        onActivated: root.cancelOrExit()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.m

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Match Day")

            // Players (the tiles) or Tactics (the board, P13b): in the title row, so the board keeps
            // the height of the screen.
            SegmentedControl {
                objectName: "boardModeSwitch"
                visible: matchDay.hasMatch && root.started
                touch: true
                model: [
                    {
                        "value": "players",
                        "text": qsTr("Players")
                    },
                    {
                        "value": "tactics",
                        "text": qsTr("Tactics")
                    }
                ]
                currentValue: root.boardMode
                Accessible.name: qsTr("Show the players as")
                onActivated: value => {
                    root.swapFrom = -1;
                    root.boardMode = value;
                }
            }

            MatchStatusBadge {
                objectName: "statusBadge"
                visible: matchDay.hasMatch
                statusKey: matchDay.statusKey
                text: matchDay.statusText
            }

            IconButton {
                objectName: "fullScreenButton"
                visible: matchDay.hasMatch
                touch: true
                iconName: root.fullScreen ? "exitFullscreen" : "fullscreen"
                text: root.fullScreen ? qsTr("Leave full screen (%1)").arg(fullScreenShortcut.nativeText) : qsTr("Full screen (%1)").arg(fullScreenShortcut.nativeText)
                onClicked: root.toggleFullScreen()
            }
        }

        RowLayout {
            objectName: "loadErrorNote"
            Layout.fillWidth: true
            visible: root.failed
            spacing: Theme.spacing.s

            Icon {
                name: "danger"
                size: Theme.iconSize.s
                color: Theme.danger
            }

            Label {
                Layout.fillWidth: true
                text: matchDay.loadError
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                wrapMode: Text.WordWrap
            }

            SecondaryButton {
                text: qsTr("Try again")
                touch: true
                onClicked: matchDay.reload()
            }
        }

        RowLayout {
            objectName: "readOnlyNote"
            Layout.fillWidth: true
            visible: matchDay.hasMatch && matchDay.readOnlyText.length > 0
            spacing: Theme.spacing.s

            Icon {
                name: "info"
                size: Theme.iconSize.s
                color: Theme.info
            }

            Label {
                Layout.fillWidth: true
                text: matchDay.readOnlyText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                wrapMode: Text.WordWrap
            }
        }

        EmptyState {
            objectName: "noMatchState"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !matchDay.hasMatch && !root.failed
            iconName: "matchday"
            title: qsTr("No match to run")
            description: root.canEditCalendar ? qsTr("Add a match in the Calendar, then set up its lineup in Pre-match.") : qsTr("There is no planned match. Ask the coach or the assistant to add one in the Calendar.")
            actionText: root.canEditCalendar ? qsTr("Open Calendar") : ""
            onActionTriggered: root.navigateRequested("calendar", {})
        }

        // A planned match: what the start needs, and the start.
        Card {
            objectName: "startCard"
            Layout.fillWidth: true
            visible: matchDay.hasMatch && matchDay.statusKey === "planned"
            title: matchDay.opponentName

            Label {
                Layout.fillWidth: true
                text: matchDay.matchDetailsText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.body
            }

            RowLayout {
                objectName: "startBlockNote"
                Layout.fillWidth: true
                visible: matchDay.startBlockText.length > 0
                spacing: Theme.spacing.s

                Icon {
                    name: "warn"
                    size: Theme.iconSize.s
                    color: Theme.warn
                }

                Label {
                    Layout.fillWidth: true
                    text: matchDay.startBlockText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                spacing: Theme.spacing.m

                PrimaryButton {
                    objectName: "startButton"
                    visible: !matchDay.readOnly
                    enabled: matchDay.canStart
                    touch: true
                    text: qsTr("Start Match Day")
                    onClicked: startDialog.open()
                }

                SecondaryButton {
                    objectName: "openPreMatchButton"
                    visible: matchDay.startBlock === "noLineup" || matchDay.startBlock === "lineupErrors"
                    touch: true
                    text: qsTr("Open Pre-match")
                    onClicked: root.navigateRequested("prematch", {
                        "matchId": matchDay.matchId
                    })
                }
            }
        }

        // A started match.
        MatchClockBar {
            objectName: "clockBar"
            Layout.fillWidth: true
            visible: matchDay.hasMatch && root.started
            teamName: AppState.teamName
            opponentName: matchDay.opponentName
            goalsFor: matchDay.goalsFor
            goalsAgainst: matchDay.goalsAgainst
            clockText: matchDay.clockText
            phaseText: matchDay.phaseText
            onPitchCount: matchDay.statusKey === "live" ? matchDay.onPitchCount : -1
            minPlayers: matchDay.minPlayers

            SecondaryButton {
                objectName: "cancelStartButton"
                visible: matchDay.canCancelStart
                touch: true
                text: qsTr("Back to planned")
                onClicked: matchDay.cancelStart()
            }

            // The clock as the referee's watch shows it: the running part moves to that time.
            SecondaryButton {
                objectName: "setClockButton"
                visible: matchDay.canSetClock
                touch: true
                text: qsTr("Set time")
                onClicked: setClockDialog.open()
            }

            // Without the clock the end is always there; with it, below 7 players (the banner).
            PrimaryButton {
                objectName: "endMatchClocklessButton"
                visible: matchDay.clockless && matchDay.canEndMatch
                touch: true
                text: qsTr("End match")
                onClicked: root.endMatch()
            }

            PrimaryButton {
                objectName: "clockButton"
                visible: matchDay.canAdvanceClock
                touch: true
                text: matchDay.clockActionText
                onClicked: matchDay.advanceClock()
            }
        }

        RowLayout {
            objectName: "actionRow"
            Layout.fillWidth: true
            visible: matchDay.hasMatch && root.started && !matchDay.readOnly
            spacing: Theme.spacing.s

            PrimaryButton {
                objectName: "goalButton"
                Layout.preferredWidth: Theme.matchActionWidth
                touch: true
                enabled: matchDay.canRecordEvents
                text: qsTr("Goal")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Goal (%1)").arg(goalShortcut.nativeText)
                onClicked: matchDay.beginAction("goal")
            }

            SecondaryButton {
                objectName: "opponentGoalButton"
                Layout.preferredWidth: Theme.matchActionWidth
                touch: true
                enabled: matchDay.canRecordEvents
                text: qsTr("Opponent goal")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Opponent goal (%1)").arg(opponentGoalShortcut.nativeText)
                onClicked: matchDay.beginAction("opponentGoal")
            }

            SecondaryButton {
                objectName: "yellowButton"
                Layout.preferredWidth: Theme.matchActionWidth
                touch: true
                enabled: matchDay.canRecordEvents
                text: qsTr("Yellow card")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Yellow card (%1)").arg(yellowShortcut.nativeText)
                onClicked: matchDay.beginAction("yellow")
            }

            SecondaryButton {
                objectName: "redButton"
                Layout.preferredWidth: Theme.matchActionWidth
                touch: true
                enabled: matchDay.canRecordEvents
                text: qsTr("Red card")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Red card (%1)").arg(redShortcut.nativeText)
                onClicked: matchDay.beginAction("red")
            }

            SecondaryButton {
                objectName: "subButton"
                Layout.preferredWidth: Theme.matchActionWidth
                touch: true
                enabled: matchDay.canSubstitute
                text: qsTr("Substitution")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Substitution (%1)").arg(subShortcut.nativeText)
                onClicked: matchDay.beginAction("sub")
            }

            SecondaryButton {
                objectName: "noteButton"
                Layout.preferredWidth: Theme.matchActionWidth
                touch: true
                enabled: matchDay.canAddNote
                text: qsTr("Note")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: qsTr("Note (%1)").arg(noteShortcut.nativeText)
                onClicked: root.addNote()
            }

            Item {
                Layout.fillWidth: true
            }

            // Always visible; disabled when there is nothing to undo.
            SecondaryButton {
                id: undoButton

                objectName: "undoButton"
                touch: true
                enabled: matchDay.canUndo
                text: qsTr("Undo")
                onClicked: root.undo()

                // The undone action names players and the opponent: user data.
                PlainToolTip {
                    objectName: "undoToolTip"
                    visible: undoButton.hovered
                    delay: Theme.tooltipDelay
                    text: matchDay.canUndo ? qsTr("Undo: %1 (%2)").arg(matchDay.undoText).arg(undoShortcut.nativeText) : qsTr("Nothing to undo (%1)").arg(undoShortcut.nativeText)
                }
            }
        }

        // Fewer than 7 players: the match cannot go on (laws of the game); only its end and undo.
        RowLayout {
            objectName: "tooFewPlayersNote"
            Layout.fillWidth: true
            visible: matchDay.tooFewPlayers
            spacing: Theme.spacing.s

            Icon {
                name: "danger"
                size: Theme.iconSize.s
                color: Theme.danger
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Fewer than %1 players on the pitch: by the laws of the game the match cannot go on. End the match, or undo the last action.").arg(matchDay.minPlayers)
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                wrapMode: Text.WordWrap
            }

            DangerButton {
                objectName: "endMatchButton"
                visible: !matchDay.readOnly
                touch: true
                text: qsTr("End match")
                onClicked: tooFewDialog.open()
            }
        }

        // An action waits for its players. In Tactics the same is in the column next to the board.
        Rectangle {
            objectName: "pendingBar"
            Layout.fillWidth: true
            visible: root.choosing && root.boardMode === "players"
            implicitHeight: pendingRow.implicitHeight + 2 * Theme.spacing.s
            radius: Theme.radius.m
            color: Theme.selection

            RowLayout {
                id: pendingRow

                anchors.fill: parent
                anchors.margins: Theme.spacing.s
                spacing: Theme.spacing.s

                Icon {
                    name: "info"
                    size: Theme.iconSize.s
                    color: Theme.info
                }

                Label {
                    objectName: "pendingPrompt"
                    Layout.fillWidth: true
                    text: matchDay.pendingPrompt
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.WordWrap
                }

                FormField {
                    id: minuteFormField

                    Layout.preferredWidth: Theme.matchMinuteFieldWidth
                    label: qsTr("Minute")
                    error: matchDay.pendingMinuteError

                    TextInputField {
                        objectName: "minuteField"
                        implicitWidth: Theme.matchMinuteFieldWidth
                        implicitHeight: Theme.hitTouch
                        maximumLength: 7
                        text: matchDay.pendingMinute
                        error: matchDay.pendingMinuteError
                        Accessible.name: minuteFormField.label
                        onTextEdited: matchDay.setPendingMinute(text)
                        onAccepted: {
                            if (root.confirmable)
                                matchDay.confirmAction();
                        }
                    }
                }

                PrimaryButton {
                    objectName: "saveActionButton"
                    visible: root.confirmable
                    touch: true
                    text: matchDay.pendingAction === "tactic" ? qsTr("Confirm") : qsTr("Save")
                    onClicked: matchDay.confirmAction()
                }

                SecondaryButton {
                    objectName: "noAssistButton"
                    visible: matchDay.pendingAction === "assist"
                    touch: true
                    text: qsTr("No assist")
                    onClicked: matchDay.skipAssist()
                }

                SecondaryButton {
                    objectName: "cancelActionButton"
                    touch: true
                    text: qsTr("Cancel")
                    ToolTip.visible: hovered
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.text: qsTr("Cancel (%1)").arg(cancelShortcut.nativeText)
                    onClicked: matchDay.cancelAction()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: matchDay.hasMatch && root.started
            spacing: Theme.spacing.l

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacing.s

                FocusFlickable {
                    id: tilesArea

                    objectName: "tilesArea"
                    visible: root.boardMode === "players"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: tilesColumn.implicitHeight

                    ColumnLayout {
                        id: tilesColumn

                        width: tilesArea.width
                        spacing: Theme.spacing.s

                        Label {
                            text: qsTr("On the pitch")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSize.caption
                            font.weight: Font.DemiBold
                        }

                        Flow {
                            objectName: "pitchTiles"
                            Layout.fillWidth: true
                            spacing: Theme.spacing.s

                            Repeater {
                                model: matchDay.pitch

                                delegate: MatchPlayerTile {
                                    id: pitchTileItem

                                    required playerId
                                    required property string name
                                    required shirtNumber
                                    required shortName
                                    required positionCode
                                    required stateKey
                                    required stateText
                                    required minutesText
                                    required goals
                                    required yellowCards
                                    required selectable
                                    required historyText

                                    objectName: "pitchTile"
                                    fullName: pitchTileItem.name
                                    choosing: root.choosing
                                    onClicked: matchDay.choosePlayer(pitchTileItem.playerId)
                                }
                            }
                        }

                        Label {
                            Layout.topMargin: Theme.spacing.s
                            text: qsTr("Bench")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSize.caption
                            font.weight: Font.DemiBold
                        }

                        Flow {
                            objectName: "benchTiles"
                            Layout.fillWidth: true
                            spacing: Theme.spacing.s

                            Repeater {
                                model: matchDay.bench

                                delegate: MatchPlayerTile {
                                    id: benchTileItem

                                    required playerId
                                    required property string name
                                    required shirtNumber
                                    required shortName
                                    required positionCode
                                    required stateKey
                                    required stateText
                                    required minutesText
                                    required goals
                                    required yellowCards
                                    required selectable
                                    required historyText

                                    objectName: "benchTile"
                                    fullName: benchTileItem.name
                                    choosing: root.choosing
                                    onClicked: matchDay.choosePlayer(benchTileItem.playerId)
                                }
                            }
                        }
                    }
                }


                // The team on the tactics board (P13b): the shape now, or the draft being edited.
                RowLayout {
                    objectName: "tacticsArea"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.boardMode === "tactics"
                    spacing: Theme.spacing.m

                    TacticsPitch {
                        id: livePitch

                        objectName: "livePitch"
                        Layout.fillHeight: true
                        Layout.preferredWidth: livePitch.height * Theme.pitch.aspect
                        model: matchDay.board
                        zones: matchDay.zones
                        interactive: matchDay.canChangeTactics
                        showEmptySlots: false
                        fitTokens: true
                        onSlotActivated: slot => root.activateLiveSlot(slot)
                        onSlotsSwapped: (from, to) => matchDay.swapSlots(from, to)
                        onSlotMoved: (slot, x, y) => matchDay.moveSlotTo(slot, x, y)
                        onPlayerDropped: (slot, playerId) => matchDay.substituteOnBoard(slot, playerId)
                    }

                    ColumnLayout {
                        Layout.preferredWidth: Theme.benchColumnWidth
                        Layout.maximumWidth: Theme.benchColumnWidth
                        Layout.fillHeight: true
                        spacing: Theme.spacing.s

                        // The pending action next to the board: the prompt, the minute and its
                        // buttons, as the bar above the tiles.
                        Rectangle {
                            objectName: "livePendingPanel"
                            Layout.fillWidth: true
                            visible: root.choosing
                            implicitHeight: livePending.implicitHeight + 2 * Theme.spacing.s
                            radius: Theme.radius.m
                            color: Theme.selection

                            ColumnLayout {
                                id: livePending

                                anchors.fill: parent
                                anchors.margins: Theme.spacing.s
                                spacing: Theme.spacing.s

                                Label {
                                    objectName: "livePendingPrompt"
                                    Layout.fillWidth: true
                                    text: matchDay.pendingPrompt
                                    textFormat: Text.PlainText
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSize.caption
                                    wrapMode: Text.WordWrap
                                }

                                FormField {
                                    id: liveMinuteFormField

                                    Layout.fillWidth: true
                                    label: qsTr("Minute")
                                    error: matchDay.pendingMinuteError

                                    TextInputField {
                                        objectName: "liveMinuteField"
                                        implicitWidth: Theme.matchMinuteFieldWidth
                                        implicitHeight: Theme.hitTouch
                                        maximumLength: 7
                                        text: matchDay.pendingMinute
                                        error: matchDay.pendingMinuteError
                                        Accessible.name: liveMinuteFormField.label
                                        onTextEdited: matchDay.setPendingMinute(text)
                                        onAccepted: {
                                            if (root.confirmable)
                                                matchDay.confirmAction();
                                        }
                                    }
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing.xs

                                    PrimaryButton {
                                        objectName: "liveSaveActionButton"
                                        visible: root.confirmable
                                        touch: true
                                        text: matchDay.pendingAction === "tactic" ? qsTr("Confirm") : qsTr("Save")
                                        onClicked: matchDay.confirmAction()
                                    }

                                    SecondaryButton {
                                        objectName: "liveNoAssistButton"
                                        visible: matchDay.pendingAction === "assist"
                                        touch: true
                                        text: qsTr("No assist")
                                        onClicked: matchDay.skipAssist()
                                    }

                                    SecondaryButton {
                                        objectName: "liveCancelActionButton"
                                        touch: true
                                        text: qsTr("Cancel")
                                        ToolTip.visible: hovered
                                        ToolTip.delay: Theme.tooltipDelay
                                        ToolTip.text: qsTr("Cancel (%1)").arg(cancelShortcut.nativeText)
                                        onClicked: {
                                            root.swapFrom = -1;
                                            matchDay.cancelAction();
                                        }
                                    }
                                }
                            }
                        }

                        Label {
                            objectName: "shapeLabel"
                            Layout.fillWidth: true
                            text: qsTr("Shape: %1").arg(matchDay.shapeText)
                            color: Theme.text
                            font.pixelSize: Theme.fontSize.body
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        // No player in the GK zone: a warning with an icon and words, never a block.
                        RowLayout {
                            objectName: "noGoalkeeperWarning"
                            Layout.fillWidth: true
                            visible: matchDay.noGoalkeeper
                            spacing: Theme.spacing.xs

                            Icon {
                                name: "warn"
                                size: Theme.iconSize.s
                                color: Theme.warn
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("No goalkeeper on the pitch")
                                color: Theme.text
                                font.pixelSize: Theme.fontSize.caption
                                wrapMode: Text.WordWrap
                            }
                        }

                        SelectField {
                            id: formationField

                            objectName: "liveFormationField"
                            Layout.fillWidth: true
                            touch: true
                            enabled: matchDay.canChangeTactics
                            model: matchDay.formationOptions
                            textRole: "text"
                            valueRole: "value"
                            value: matchDay.shapeFormation
                            Accessible.name: qsTr("Formation")
                            onValueActivated: value => {
                                if (value === "custom" || !matchDay.changeFormation(value))
                                    formationField.showValue();
                            }
                        }

                        // The selected slot: its position (the keyboard path of a drag to another
                        // zone) and the swap (P12).
                        ColumnLayout {
                            objectName: "liveSlotPanel"
                            Layout.fillWidth: true
                            visible: matchDay.selectedSlot >= 0
                            spacing: Theme.spacing.s

                            Label {
                                Layout.fillWidth: true
                                text: matchDay.selectedSlot >= 0 ? qsTr("Selected: %1").arg(matchDay.board.get(matchDay.selectedSlot).fullName ?? "") : ""
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: Theme.text
                                font.pixelSize: Theme.fontSize.body
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacing.xs

                                SelectField {
                                    id: livePositionField

                                    objectName: "livePositionField"
                                    Layout.fillWidth: true
                                    touch: true
                                    enabled: matchDay.canChangeTactics
                                    model: matchDay.positionOptions
                                    textRole: "text"
                                    valueRole: "value"
                                    value: matchDay.selectedPosition
                                    Accessible.name: qsTr("Position")
                                    // A refused position (a second goalkeeper) shows the current one.
                                    onValueActivated: value => {
                                        if (!matchDay.setSlotPosition(matchDay.selectedSlot, value))
                                            livePositionField.showValue();
                                    }
                                }

                                IconButton {
                                    objectName: "liveSwapButton"
                                    touch: true
                                    enabled: matchDay.canChangeTactics
                                    iconName: "swap"
                                    iconColor: root.swapFrom >= 0 ? Theme.accent : Theme.text
                                    text: root.swapFrom >= 0 ? qsTr("Now choose the player to swap with") : qsTr("Swap places with another player")
                                    onClicked: root.swapFrom = root.swapFrom >= 0 ? -1 : matchDay.selectedSlot
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: !root.choosing
                            text: matchDay.canChangeTactics ? qsTr("Drag a player to another zone, or onto another player to swap places. Drag a bench player onto a player to substitute.") : qsTr("The shape can be changed while a part runs or in a break.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSize.caption
                            wrapMode: Text.WordWrap
                        }

                        Label {
                            text: qsTr("Bench")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSize.caption
                            font.weight: Font.DemiBold
                        }

                        FocusFlickable {
                            id: liveBenchArea

                            objectName: "liveBench"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentHeight: liveBenchColumn.implicitHeight

                            Column {
                                id: liveBenchColumn

                                width: liveBenchArea.width
                                spacing: Theme.spacing.s

                                Repeater {
                                    model: matchDay.bench

                                    delegate: MatchPlayerTile {
                                        id: liveBenchTile

                                        required playerId
                                        required property string name
                                        required shirtNumber
                                        required shortName
                                        required positionCode
                                        required stateKey
                                        required stateText
                                        required minutesText
                                        required goals
                                        required yellowCards
                                        required selectable
                                        required historyText

                                        objectName: "liveBenchTile"
                                        width: liveBenchColumn.width
                                        fullName: liveBenchTile.name
                                        choosing: root.choosing
                                        dragEnabled: matchDay.canSubstitute && liveBenchTile.stateKey === "bench" && !matchDay.tacticsDirty
                                        onClicked: matchDay.choosePlayer(liveBenchTile.playerId)
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }
            }

            // Not filled: the ListView inside would otherwise make it take the width of the tiles.
            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: Theme.matchTimelineWidth
                Layout.fillHeight: true
                spacing: Theme.spacing.s

                Label {
                    text: qsTr("Events")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                    font.weight: Font.DemiBold
                }

                MatchTimeline {
                    objectName: "timeline"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: matchDay.timeline
                    notesEditable: matchDay.canAddNote
                    onNoteEditRequested: (noteId, minute, playerId, text) => noteDialog.openForEditing(noteId, minute, playerId, text)
                    onNoteDeleteRequested: noteId => root.deleteNote(noteId)
                }
            }
        }
    }

    MatchStartDialog {
        id: startDialog

        objectName: "startDialog"
        formatOptions: matchDay.formatOptions
        defaultFormat: matchDay.defaultFormat
        minPeriods: matchDay.minPeriods
        maxPeriods: matchDay.maxPeriods
        minPeriodLength: matchDay.minPeriodLength
        maxPeriodLength: matchDay.maxPeriodLength
        defaultClockless: matchDay.defaultClockless
        onStartRequested: (format, periods, periodLength, clockless) => {
            if (matchDay.start(format, periods, periodLength, clockless))
                startDialog.close();
        }
    }

    MatchNoteDialog {
        id: noteDialog

        objectName: "noteDialog"
        playerOptions: matchDay.notePlayerOptions
        minuteError: noteDialog.visible ? matchDay.noteMinuteError(noteDialog.minuteText) : ""
        onSaveRequested: (noteId, minute, playerId, text) => {
            const saved = noteId > 0 ? matchDay.updateNote(noteId, minute, playerId, text) : matchDay.addNote(minute, playerId, text);
            if (saved)
                noteDialog.close();
        }
    }

    // An action left fewer than 7 players on the pitch.
    AppDialog {
        id: tooFewDialog

        objectName: "tooFewPlayersDialog"
        size: AppDialog.Small
        touch: true
        title: qsTr("Fewer than %1 players").arg(matchDay.minPlayers)
        primaryText: qsTr("End match")
        secondaryText: qsTr("Not now")
        onOpened: endMinuteField.text = matchDay.endMinute
        onPrimaryClicked: {
            if (matchDay.endMatch(matchDay.clockless ? endMinuteField.text : ""))
                tooFewDialog.close();
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("By the laws of the game the match cannot go on (fewer than %1 players). End the match?").arg(matchDay.minPlayers)
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }

        // Without the clock: the minute the match ended, the last event at the least.
        FormField {
            id: endMinuteFormField

            Layout.fillWidth: true
            visible: matchDay.clockless
            label: qsTr("Minute of the end")
            helpText: qsTr("Not before the last event (%1').").arg(matchDay.endMinute)

            TextInputField {
                id: endMinuteField

                objectName: "endMinuteField"
                implicitWidth: Theme.matchMinuteFieldWidth
                implicitHeight: Theme.hitTouch
                maximumLength: 3
                // Enter reaches the dialog, which saves (AppDialog).
                Accessible.name: endMinuteFormField.label
            }
        }
    }

    // "Set time": the time of the running part as the clock (the referee's watch) shows it.
    AppDialog {
        id: setClockDialog

        objectName: "setClockDialog"
        size: AppDialog.Small
        touch: true
        title: qsTr("Set time")
        primaryText: qsTr("Set")
        primaryEnabled: matchDay.clockTextError(clockTimeField.text).length === 0
        onOpened: {
            clockTimeField.text = matchDay.clockText;
            clockTimeField.forceActiveFocus();
            clockTimeField.selectAll();
        }
        onPrimaryClicked: {
            if (root.setClock(clockTimeField.text))
                setClockDialog.close();
        }

        FormField {
            id: clockTimeFormField

            Layout.fillWidth: true
            label: qsTr("Time of the clock")
            helpText: qsTr("As the clock shows it: 23:41, or 45+2:10 in stoppage time. The clock runs on from there.")
            error: clockTimeField.text.length > 0 ? matchDay.clockTextError(clockTimeField.text) : ""

            TextInputField {
                id: clockTimeField

                objectName: "clockTimeField"
                implicitWidth: Theme.fieldWidth / 2
                implicitHeight: Theme.hitTouch
                maximumLength: 9
                error: clockTimeFormField.error
                // Enter reaches the dialog, which saves (AppDialog).
                Accessible.name: clockTimeFormField.label
            }
        }
    }

    AppDialog {
        id: leaveQuestion

        objectName: "leaveQuestion"
        size: AppDialog.Small
        touch: true
        title: qsTr("Leave the live match?")
        primaryText: qsTr("Leave")
        secondaryText: qsTr("Stay")
        onPrimaryClicked: {
            leaveQuestion.close();
            root.leaveAccepted();
        }
        onRejected: root.leaveCancelled()

        Label {
            Layout.fillWidth: true
            text: qsTr("The match is live. The clock keeps running, and you can come back to Match Day from the menu.")
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }
}
