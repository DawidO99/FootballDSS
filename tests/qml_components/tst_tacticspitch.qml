import QtQuick
import QtTest
import FootballDSS

// TacticsPitch and PlayerToken (P12): taps, double taps and keys on tokens, a token dragged to
// another zone (decision 12), also a free slot, a token dropped on another one, and a read-only
// board. Match Day (P13b): hidden empty slots, the badges of cards and goals, a bench player dropped
// on a token.
TestCase {
    id: testCase

    name: "TacticsPitch"
    // Tab and the mouse skip what is not visible, and a TestCase is not by default.
    visible: true
    width: 640
    height: 640
    when: windowShown

    Component {
        id: hostComponent

        Item {
            property var activations: []
            property var pointers: []
            property var doubleTaps: []
            property var moves: []
            property var swaps: []
            property var removals: []
            property var drops: []
            property alias pitch: pitch
            property alias model: slotsModel

            width: 360
            height: 480

            ListModel {
                id: slotsModel

                // A left midfielder and a centre midfielder of a 4-4-2, and a free left winger slot.
                ListElement { slotIndex: 0; positionText: "LM"; pitchX: 0.125; pitchY: 0.48; playerId: 9; shirtNumber: 11; shortName: "Lewy"; fullName: "Adam Lewy"; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false }
                ListElement { slotIndex: 1; positionText: "CM"; pitchX: 0.625; pitchY: 0.48; playerId: 7; shirtNumber: 8; shortName: "Środek"; fullName: "Bartosz Środek"; severity: "warning"; issueText: "x"; selected: false; yellowCards: 0; goals: 0; hidden: false }
                ListElement { slotIndex: 2; positionText: "ST"; pitchX: 0.5; pitchY: 0.12; playerId: 0; shirtNumber: 0; shortName: ""; fullName: ""; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false }
            }

            TacticsPitch {
                id: pitch

                anchors.fill: parent
                model: slotsModel
                // The rows of LM CM RM and of LW AM RW, as PreMatchViewModel gives them.
                zones: [
                    { "position": "lm", "text": "LM", "x": 0, "y": 0.4, "width": 0.25, "height": 0.16 },
                    { "position": "cm", "text": "CM", "x": 0.25, "y": 0.4, "width": 0.5, "height": 0.16 },
                    { "position": "lw", "text": "LW", "x": 0, "y": 0.24, "width": 0.25, "height": 0.16 }
                ]
                onSlotActivated: (slot, pointer) => {
                    parent.activations = parent.activations.concat([slot]);
                    parent.pointers = parent.pointers.concat([pointer]);
                }
                onSlotDoubleTapped: slot => parent.doubleTaps = parent.doubleTaps.concat([slot])
                onSlotMoved: (slot, x, y) => parent.moves = parent.moves.concat([{ "slot": slot, "x": x, "y": y }])
                onSlotsSwapped: (from, to) => parent.swaps = parent.swaps.concat([[from, to]])
                onRemoveRequested: slot => parent.removals = parent.removals.concat([slot])
                onPlayerDropped: (slot, playerId) => parent.drops = parent.drops.concat([[slot, playerId]])
            }
        }
    }

    Component {
        id: benchTileComponent

        MatchPlayerTile {
            playerId: 12
            shirtNumber: 12
            shortName: "Zmiennik"
            fullName: "Jan Zmiennik"
            positionCode: "CM"
            stateKey: "bench"
            dragEnabled: true
        }
    }

    function createHost() {
        const host = createTemporaryObject(hostComponent, testCase);
        verify(host);
        waitForRendering(host);
        return host;
    }

    function init() {
        failOnWarning(/.?/);
    }

    // The centre of the circle of a token, in the coordinates of `item`.
    function tokenCentre(host, slot, item) {
        const token = host.pitch.tokenAt(slot);
        verify(token);
        return token.mapToItem(item, token.width / 2, Theme.pitch.tokenSize / 2);
    }

    // A drag with the mouse from one point of the pitch to another, in small steps.
    function drag(host, from, to) {
        const pitchItem = host.pitch.pitchItem;
        mousePress(pitchItem, from.x, from.y, Qt.LeftButton);
        const steps = 12;
        for (let i = 1; i <= steps; ++i)
            mouseMove(pitchItem, from.x + (to.x - from.x) * i / steps, from.y + (to.y - from.y) * i / steps, 10, Qt.LeftButton);
        mouseRelease(pitchItem, to.x, to.y, Qt.LeftButton);
    }

    function test_tap_on_a_token_with_a_player() {
        const host = createHost();
        const centre = tokenCentre(host, 0, host);
        mouseClick(host, centre.x, centre.y);
        compare(host.activations, [0]);
        // Again, with the pointer already over it (its tooltip may be up).
        wait(Theme.tooltipDelay + 100);
        mouseClick(host, centre.x, centre.y);
        compare(host.activations, [0, 0]);
        compare(host.pointers, [true, true]);
        compare(host.doubleTaps, []);
    }

    // QtTest spaces its clicks by the double click interval, so a TapHandler never sees a double
    // tap from here; qml_smoke sends one with its own timestamps. This checks the way to the board.
    function test_double_tap_of_a_token_reaches_the_board() {
        const host = createHost();
        host.pitch.tokenAt(1).doubleTapped();
        compare(host.doubleTaps, [1]);
        compare(host.activations, []);
    }

    function test_tap_on_a_free_slot() {
        const host = createHost();
        const centre = tokenCentre(host, 2, host);
        mouseClick(host, centre.x, centre.y);
        compare(host.activations, [2]);
    }

    function test_keys_on_a_token() {
        const host = createHost();
        host.pitch.focusSlot(1);
        verify(host.pitch.tokenAt(1).activeFocus);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Space);
        compare(host.activations, [1, 1]);
        compare(host.pointers, [false, false]);
        keyClick(Qt.Key_Delete);
        compare(host.removals, [1]);
        // Tab moves to the next token.
        keyClick(Qt.Key_Tab);
        verify(host.pitch.tokenAt(2).activeFocus);
    }

    function test_drag_to_another_zone_moves_the_slot() {
        const host = createHost();
        const pitchItem = host.pitch.pitchItem;
        const from = tokenCentre(host, 0, pitchItem);
        // Into the left winger zone, above the left midfielder.
        const to = Qt.point(pitchItem.width * 0.1, pitchItem.height * 0.3);
        mousePress(pitchItem, from.x, from.y, Qt.LeftButton);
        for (let i = 1; i <= 12; ++i)
            mouseMove(pitchItem, from.x + (to.x - from.x) * i / 12, from.y + (to.y - from.y) * i / 12, 10, Qt.LeftButton);
        // The zones show while the token is dragged.
        verify(host.pitch.dragging);
        compare(host.pitch.zones[host.pitch.hoverZone].position, "lw");
        mouseRelease(pitchItem, to.x, to.y, Qt.LeftButton);
        verify(!host.pitch.dragging);
        compare(host.moves.length, 1);
        compare(host.moves[0].slot, 0);
        verify(host.moves[0].x < 0.25);
        verify(host.moves[0].y >= 0.24 && host.moves[0].y < 0.4);
        compare(host.activations, []);
    }

    // A free slot moves like a taken one (P12 manual test).
    function test_drag_a_free_slot_to_another_zone() {
        const host = createHost();
        const pitchItem = host.pitch.pitchItem;
        drag(host, tokenCentre(host, 2, pitchItem), Qt.point(pitchItem.width * 0.1, pitchItem.height * 0.3));
        compare(host.moves.length, 1);
        compare(host.moves[0].slot, 2);
        verify(host.moves[0].x < 0.25);
        compare(host.activations, []);
    }

    function test_drop_on_another_token_swaps() {
        const host = createHost();
        const pitchItem = host.pitch.pitchItem;
        drag(host, tokenCentre(host, 0, pitchItem), tokenCentre(host, 1, pitchItem));
        compare(host.swaps, [[0, 1]]);
        compare(host.moves, []);
    }

    function test_read_only_board_does_not_drag() {
        const host = createHost();
        host.pitch.interactive = false;
        const pitchItem = host.pitch.pitchItem;
        drag(host, tokenCentre(host, 0, pitchItem), Qt.point(pitchItem.width * 0.1, pitchItem.height * 0.3));
        compare(host.moves, []);
        compare(host.swaps, []);
        verify(!host.pitch.dragging);
        // It still selects, and Delete does nothing.
        const centre = tokenCentre(host, 0, host);
        mouseClick(host, centre.x, centre.y);
        compare(host.activations, [0]);
        keyClick(Qt.Key_Delete);
        compare(host.removals, []);
    }

    // Match Day (P13b): a player sent off leaves his slot empty, and the live board does not draw it.
    function test_hidden_slots_are_not_drawn_on_the_live_board() {
        const host = createHost();
        host.model.setProperty(2, "hidden", true);
        verify(host.pitch.tokenAt(2).visible);
        host.pitch.showEmptySlots = false;
        verify(!host.pitch.tokenAt(2).visible);
        verify(host.pitch.tokenAt(0).visible);
        // Tab skips it.
        host.pitch.focusSlot(1);
        keyClick(Qt.Key_Tab);
        verify(!host.pitch.tokenAt(2).activeFocus);
    }

    function test_cards_and_goals_are_icons_with_words() {
        const host = createHost();
        const token = host.pitch.tokenAt(0);
        verify(!findChild(token, "matchBadges").visible);
        host.model.setProperty(0, "yellowCards", 1);
        host.model.setProperty(0, "goals", 2);
        verify(findChild(token, "matchBadges").visible);
        verify(findChild(token, "yellowBadge").visible);
        verify(findChild(token, "goalBadge").visible);
        verify(token.description.indexOf("Yellow card") >= 0);
        verify(token.description.indexOf("Goals: 2") >= 0);
        verify(token.Accessible.name.indexOf("Yellow card") >= 0);
    }

    function test_a_bench_tile_dropped_on_a_token() {
        const host = createHost();
        const tile = createTemporaryObject(benchTileComponent, testCase, { "x": 380, "y": 10 });
        verify(tile);
        waitForRendering(tile);
        const target = tokenCentre(host, 1, testCase);
        const from = Qt.point(tile.x + tile.width / 2, tile.y + tile.height / 2);
        mousePress(testCase, from.x, from.y, Qt.LeftButton);
        for (let i = 1; i <= 12; ++i)
            mouseMove(testCase, from.x + (target.x - from.x) * i / 12, from.y + (target.y - from.y) * i / 12, 10, Qt.LeftButton);
        verify(tile.dragging);
        mouseRelease(testCase, target.x, target.y, Qt.LeftButton);
        verify(!tile.dragging);
        compare(host.drops.length, 1);
        compare(host.drops[0][0], 1);
        compare(host.drops[0][1], 12);
        compare(host.swaps, []);
    }

    function test_a_bench_tile_without_drag_does_not_drag() {
        const host = createHost();
        const tile = createTemporaryObject(benchTileComponent, testCase, { "x": 380, "y": 10, "dragEnabled": false });
        verify(tile);
        waitForRendering(tile);
        const target = tokenCentre(host, 1, testCase);
        const from = Qt.point(tile.x + tile.width / 2, tile.y + tile.height / 2);
        mousePress(testCase, from.x, from.y, Qt.LeftButton);
        for (let i = 1; i <= 12; ++i)
            mouseMove(testCase, from.x + (target.x - from.x) * i / 12, from.y + (target.y - from.y) * i / 12, 10, Qt.LeftButton);
        verify(!tile.dragging);
        mouseRelease(testCase, target.x, target.y, Qt.LeftButton);
        compare(host.drops, []);
    }
}
