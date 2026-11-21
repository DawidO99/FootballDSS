import QtQuick
import QtTest
import FootballDSS

// MatchTimeline (P13): a note row can be edited and deleted (touch-sized buttons) while the notes
// are editable; an event row cannot. Texts of user data are plain. In the report (P14) events and
// changes of shape can be deleted, only when switched on.
TestCase {
    id: testCase

    name: "MatchTimeline"
    width: 400
    height: 300
    visible: true
    when: windowShown

    ListModel {
        id: rows

        ListElement { kind: "note"; eventId: 0; noteId: 7; minuteText: "12'"; typeKey: "note"; iconName: "note"; typeText: "Note"; playerText: "Jan Gol"; relatedText: "<b>Kostka</b>"; noteMinute: "12"; notePlayerId: 3; noteText: "<b>Kostka</b>"; changeId: 0 }
        ListElement { kind: "event"; eventId: 5; noteId: 0; minuteText: "11'"; typeKey: "goal"; iconName: "goal"; typeText: "Goal"; playerText: "Jan Gol"; relatedText: ""; noteMinute: ""; notePlayerId: 0; noteText: ""; changeId: 0 }
        ListElement { kind: "tactic"; eventId: 0; noteId: 0; minuteText: "60'"; typeKey: "tactic"; iconName: "tactic"; typeText: "Change of shape"; playerText: "4-4-2 → 4-3-3"; relatedText: ""; noteMinute: ""; notePlayerId: 0; noteText: ""; changeId: 9 }
    }

    Component {
        id: timelineComponent

        MatchTimeline {
            property var edited: []
            property double deleted: 0
            property double deletedEvent: 0
            property double deletedChange: 0

            width: 360
            height: 280
            model: rows
            onNoteEditRequested: (noteId, minute, playerId, text) => edited = [noteId, minute, playerId, text]
            onNoteDeleteRequested: noteId => deleted = noteId
            onEventDeleteRequested: eventId => deletedEvent = eventId
            onTacticDeleteRequested: changeId => deletedChange = changeId
        }
    }

    function buttonsOf(timeline, name) {
        const found = [];
        const walk = item => {
            if (item.objectName === name && item.visible)
                found.push(item);
            for (let i = 0; i < item.children.length; ++i)
                walk(item.children[i]);
        };
        walk(timeline.contentItem);
        return found;
    }

    function init() {
        failOnWarning(/.?/);
    }

    function test_note_buttons_only_when_editable() {
        const timeline = createTemporaryObject(timelineComponent, testCase);
        waitForRendering(timeline);
        compare(buttonsOf(timeline, "editNoteButton").length, 0);
        timeline.notesEditable = true;
        waitForRendering(timeline);
        const edit = buttonsOf(timeline, "editNoteButton");
        const remove = buttonsOf(timeline, "deleteNoteButton");
        compare(edit.length, 1); // the note row only
        compare(remove.length, 1);
        verify(edit[0].height >= Theme.hitTouch);
        mouseClick(edit[0]);
        compare(timeline.edited[0], 7);
        compare(timeline.edited[1], "12");
        compare(timeline.edited[2], 3);
        compare(timeline.edited[3], "<b>Kostka</b>");
        mouseClick(remove[0]);
        compare(timeline.deleted, 7);
    }

    function test_events_and_changes_deleted_only_when_switched_on() {
        const timeline = createTemporaryObject(timelineComponent, testCase);
        waitForRendering(timeline);
        compare(buttonsOf(timeline, "deleteEventButton").length, 0);
        timeline.eventsDeletable = true;
        waitForRendering(timeline);
        let buttons = buttonsOf(timeline, "deleteEventButton");
        compare(buttons.length, 1); // the event row only
        mouseClick(buttons[0]);
        compare(timeline.deletedEvent, 5);
        timeline.tacticsDeletable = true;
        waitForRendering(timeline);
        buttons = buttonsOf(timeline, "deleteEventButton");
        compare(buttons.length, 2);
        mouseClick(buttons[1]);
        compare(timeline.deletedChange, 9);
        compare(timeline.deletedEvent, 5);
    }
}
