pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The match report (SPEC section 4, packet P14): the score with the opponent, date and competition;
// the ratings grid (rating 1–10 and RPE 0–10 for every player who played, by keyboard as the
// training grid); the note; the timeline with the corrections (add or delete an event, notes,
// delete a change of shape) and the shapes with their minutes. A section without a sidebar item:
// navigationArgs.matchId opens a match, navigationArgs.returnTo ({ key, args }) is where Back
// leads. Ratings, RPE and the note are saved with Save (Ctrl+S); leaving with changes asks.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})
    property var returnTo: null
    property int lastRow: -1
    property int lastColumn: MatchRatingsModel.RatingColumn
    readonly property bool failed: report.loadError.length > 0
    readonly property bool hasUnsavedChanges: report.dirty
    readonly property bool dialogOpen: leaveQuestion.visible || correctionDialog.visible || noteDialog.visible || deleteGoalQuestion.visible
    readonly property alias viewModel: report
    readonly property alias grid: ratingsGrid
    readonly property alias leaveDialog: leaveQuestion
    readonly property alias correctionEditor: correctionDialog
    readonly property alias noteEditor: noteDialog
    readonly property alias deleteGoalDialog: deleteGoalQuestion
    readonly property alias noteInput: noteField

    signal navigateRequested(string key, var args)
    // The answer to askBeforeLeaving(): the changes are saved or dropped, or the user stays.
    signal leaveAccepted
    signal leaveCancelled

    function askBeforeLeaving() {
        leaveQuestion.open();
    }

    function backText() {
        const key = root.returnTo ? root.returnTo.key : "calendar";
        if (key === "squad")
            return qsTr("← Player card");
        if (key === "matchday")
            return qsTr("← Match Day");
        return qsTr("← Calendar");
    }

    function goBack() {
        if (root.returnTo)
            root.navigateRequested(root.returnTo.key, root.returnTo.args ?? {});
        else
            root.navigateRequested("calendar", {});
    }

    function focusContent() {
        if (ratingsGrid.visible)
            ratingsGrid.focusFirstEditable();
    }

    function save() {
        if (report.save()) {
            if (!ratingsGrid.activeFocus && root.lastRow >= 0)
                ratingsGrid.focusCell(root.lastRow, root.lastColumn);
            return true;
        }
        const row = report.firstErrorRow();
        if (row >= 0)
            ratingsGrid.focusCell(row, MatchRatingsModel.RatingColumn);
        return false;
    }

    function deleteEvent(eventId) {
        if (report.eventRemovesGoal(eventId)) {
            deleteGoalQuestion.eventId = eventId;
            deleteGoalQuestion.open();
            return;
        }
        report.deleteEvent(eventId);
    }

    function showNote() {
        if (noteField.text !== report.note)
            noteField.text = report.note;
    }

    // Not from the visible change: the order against it is not guaranteed, see Main.qml.
    onNavigationArgsChanged: {
        if (navigationArgs.matchId === undefined)
            return;
        root.returnTo = navigationArgs.returnTo ?? null;
        report.openMatch(navigationArgs.matchId);
        root.showNote();
        Qt.callLater(root.focusContent);
    }
    // All views stay loaded, and Match Day may have changed the match meanwhile.
    onVisibleChanged: {
        if (visible && report.matchId > 0) {
            report.reload();
            root.showNote();
        }
    }

    MatchReportViewModel {
        id: report

        onErrorOccurred: message => Toasts.error(message)
        onDeleted: text => Toasts.show(text, qsTr("Undo"), () => report.undoDelete())
        onNoteChanged: root.showNote()
    }

    Shortcut {
        id: saveShortcut

        objectName: "reportSaveShortcut"
        sequence: "Ctrl+S"
        enabled: root.visible && report.dirty && !root.dialogOpen
        onActivated: root.save()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            SecondaryButton {
                objectName: "reportBackButton"
                text: root.backText()
                onClicked: root.goBack()
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    objectName: "reportTitle"
                    Layout.fillWidth: true
                    text: report.hasReport ? qsTr("%1:%2 against %3").arg(report.goalsFor).arg(report.goalsAgainst).arg(report.opponentName) : qsTr("Match report")
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.h1
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible: report.hasReport
                    text: [report.dateText, report.venueText, report.competition, report.formatText].filter(part => part.length > 0).join(" · ")
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    elide: Text.ElideRight
                }
            }

            StatusBadge {
                objectName: "resultBadge"
                visible: report.resultKey.length > 0
                kind: report.resultKey === "win" ? StatusBadge.Ok : report.resultKey === "loss" ? StatusBadge.Danger : StatusBadge.Neutral
                text: report.resultText
            }

            StatusBadge {
                objectName: "reportSaveState"
                visible: report.hasReport && !report.readOnly
                kind: report.dirty ? StatusBadge.Warn : StatusBadge.Ok
                text: report.dirty ? qsTr("Not saved") : report.ratedText
            }

            PrimaryButton {
                objectName: "reportSaveButton"
                visible: report.hasReport && !report.readOnly
                enabled: report.dirty
                text: qsTr("Save")
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Save (%1)").arg(saveShortcut.nativeText)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.save()
            }
        }

        EmptyState {
            objectName: "reportLoadFailed"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.failed
            title: qsTr("The match report could not be read")
            description: report.loadError
            actionText: qsTr("Try again")
            onActionTriggered: report.reload()
        }

        EmptyState {
            objectName: "reportNoMatch"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && !report.hasReport
            iconName: "matchday"
            title: qsTr("No match chosen")
            description: qsTr("Open a finished match from the Calendar to see its report.")
            actionText: qsTr("Open the calendar")
            onActionTriggered: root.navigateRequested("calendar", {})
        }

        EmptyState {
            objectName: "reportNotFinished"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && report.hasReport && report.notFinished
            iconName: "matchday"
            title: qsTr("This match is not finished")
            description: qsTr("The match is live again in Match Day. Finish it there, then the report opens.")
            actionText: qsTr("Back to Match Day")
            onActionTriggered: root.navigateRequested("matchday", {
                "matchId": report.matchId
            })
        }

        Label {
            objectName: "reportReadOnly"
            Layout.fillWidth: true
            visible: report.hasReport && !report.notFinished && report.readOnly
            text: report.readOnlyText
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.failed && report.hasReport && !report.notFinished
            spacing: Theme.spacing.l

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                spacing: Theme.spacing.m

                MatchRatingsGrid {
                    id: ratingsGrid

                    objectName: "ratingsGrid"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: report.ratings
                    readOnly: report.readOnly
                    Accessible.name: qsTr("Ratings and RPE")
                    onDigitEntered: (row, column, digit) => report.enterDigit(row, column, digit)
                    onCellCleared: (row, column) => report.clearCell(row, column)
                    onCellEntered: (row, column) => {
                        root.lastRow = row;
                        root.lastColumn = column;
                        report.rememberCell(row, column);
                    }
                    onCellReverted: (row, column) => report.revertCell(row, column)
                }

                FormField {
                    id: noteFormField

                    Layout.fillWidth: true
                    label: qsTr("Note of the report")
                    helpText: qsTr("%1 / %2 characters").arg(noteField.text.trim().length).arg(report.noteMaxLength)
                    error: noteField.text.trim().length > report.noteMaxLength ? qsTr("At most %1 characters.").arg(report.noteMaxLength) : ""

                    TextAreaField {
                        id: noteField

                        objectName: "reportNoteField"
                        Layout.fillWidth: true
                        enabled: !report.readOnly
                        placeholderText: qsTr("What went well, what to work on")
                        error: noteFormField.error
                        Accessible.name: noteFormField.label
                        onTextChanged: report.setNote(text)
                        onAccepted: root.save()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: Theme.spacing.m

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: qsTr("Events")

                    actions: [
                        SecondaryButton {
                            objectName: "addEventButton"
                            visible: report.correctable
                            text: qsTr("Add event")
                            onClicked: correctionDialog.openForAdding()
                        },
                        SecondaryButton {
                            objectName: "addReportNoteButton"
                            visible: !report.readOnly
                            text: qsTr("Add note")
                            onClicked: noteDialog.openForAdding(report.lastMinute)
                        }
                    ]

                    Label {
                        objectName: "notCorrectable"
                        Layout.fillWidth: true
                        visible: !report.readOnly && !report.correctable
                        text: report.notCorrectableText
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                        wrapMode: Text.Wrap
                    }

                    MatchTimeline {
                        objectName: "reportTimeline"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 3 * Theme.rowHeight
                        model: report.timeline
                        emptyText: qsTr("No events in this match.")
                        notesEditable: !report.readOnly
                        eventsDeletable: report.correctable
                        tacticsDeletable: report.correctable
                        onNoteEditRequested: (noteId, minute, playerId, text) => noteDialog.openForEditing(noteId, minute, playerId, text)
                        onNoteDeleteRequested: noteId => report.deleteNote(noteId)
                        onEventDeleteRequested: eventId => root.deleteEvent(eventId)
                        onTacticDeleteRequested: changeId => report.deleteTacticChange(changeId)
                    }
                }

                Card {
                    objectName: "shapesCard"
                    Layout.fillWidth: true
                    visible: shapesList.count > 0
                    title: qsTr("Shapes")

                    Repeater {
                        id: shapesList

                        model: report.shapes

                        delegate: RowLayout {
                            id: shapeRow

                            required property string formation
                            required property string fromText
                            required property string toText
                            required property int minutes

                            Layout.fillWidth: true
                            spacing: Theme.spacing.s

                            Icon {
                                name: "tactic"
                                size: Theme.iconSize.s
                                color: Theme.textMuted
                            }

                            Label {
                                text: shapeRow.formation
                                color: Theme.text
                                font.pixelSize: Theme.fontSize.body
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("%1 – %2 (%3 min)").arg(shapeRow.fromText).arg(shapeRow.toText).arg(shapeRow.minutes)
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.body
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }

    MatchCorrectionDialog {
        id: correctionDialog

        objectName: "correctionDialog"
        viewModel: report
    }

    MatchNoteDialog {
        id: noteDialog

        objectName: "reportNoteDialog"
        playerOptions: report.notePlayerOptions
        minuteError: report.minuteError(noteDialog.minuteText)
        onSaveRequested: (noteId, minute, playerId, text) => {
            const done = noteId > 0 ? report.updateNote(noteId, minute, playerId, text) : report.addNote(minute, playerId, text);
            if (done)
                noteDialog.close();
        }
    }

    // Deleting a goal changes the score: it asks first (AGENTS section 6).
    AppDialog {
        id: deleteGoalQuestion

        property double eventId: 0

        objectName: "deleteGoalQuestion"
        size: AppDialog.Small
        title: qsTr("Delete the goal?")
        primaryText: ""
        dangerText: qsTr("Delete goal")
        onDangerClicked: {
            deleteGoalQuestion.close();
            report.deleteEvent(deleteGoalQuestion.eventId);
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("The score of the match changes. The goal can be brought back from the message that follows.")
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }

    AppDialog {
        id: leaveQuestion

        objectName: "reportLeaveQuestion"
        size: AppDialog.Small
        title: qsTr("Save the changes?")
        primaryText: qsTr("Save")
        secondaryText: qsTr("Stay")
        dangerText: qsTr("Discard changes")
        onPrimaryClicked: {
            leaveQuestion.close();
            if (root.save())
                root.leaveAccepted();
            else
                root.leaveCancelled();
        }
        onDangerClicked: {
            leaveQuestion.close();
            report.discardChanges();
            root.leaveAccepted();
        }
        onRejected: root.leaveCancelled()

        Label {
            Layout.fillWidth: true
            text: qsTr("The ratings, RPE or note of this match have changes that are not saved.")
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }
}
