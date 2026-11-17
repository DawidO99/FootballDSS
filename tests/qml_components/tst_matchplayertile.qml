import QtQuick
import QtTest
import FootballDSS

// MatchPlayerTile (P13): a touch-sized tile; while an action waits for a player only the tiles that
// fit it can be chosen; a player who left the pitch says why in a word, not only by colour.
TestCase {
    id: testCase

    name: "MatchPlayerTile"
    width: 400
    height: 200
    visible: true
    when: windowShown

    Component {
        id: tileComponent

        MatchPlayerTile {
            property int clicks: 0

            shirtNumber: 9
            shortName: "Gol"
            fullName: "Igor Gol"
            positionCode: "ST"
            stateText: "On the pitch"
            minutesText: "47'"
            onClicked: clicks += 1
        }
    }

    function createTile() {
        const tile = createTemporaryObject(tileComponent, testCase);
        verify(tile);
        waitForRendering(tile);
        return tile;
    }

    function init() {
        failOnWarning(/.?/);
    }

    function test_is_a_touch_target() {
        const tile = createTile();
        verify(tile.height >= Theme.hitTouch);
        verify(tile.width >= Theme.hitTouch);
    }

    function test_click_and_keys_activate() {
        const tile = createTile();
        mouseClick(tile);
        tile.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Space);
        compare(tile.clicks, 3);
    }

    function test_only_fitting_tiles_can_be_chosen() {
        const tile = createTile();
        tile.choosing = true;
        tile.selectable = false;
        verify(!tile.enabled);
        mouseClick(tile);
        compare(tile.clicks, 0);
        tile.selectable = true;
        verify(tile.enabled);
        mouseClick(tile);
        compare(tile.clicks, 1);
        tile.choosing = false;
        tile.selectable = false;
        verify(tile.enabled);
    }

    function test_leaving_the_pitch_is_a_word() {
        const tile = createTile();
        const state = findChild(tile, "tileState");
        verify(state);
        verify(!state.visible);
        tile.stateKey = "sentOff";
        tile.stateText = "Sent off";
        verify(state.visible);
        compare(state.text, "Sent off");
        verify(tile.Accessible.name.includes("Sent off"));
        verify(tile.Accessible.name.includes("Igor Gol"));
    }

    function test_yellow_card_shows_while_on_the_pitch() {
        const tile = createTile();
        const yellow = findChild(tile, "tileYellow");
        verify(!yellow.visible);
        tile.yellowCards = 1;
        verify(yellow.visible);
        tile.stateKey = "sentOff";
        verify(!yellow.visible);
    }

    function test_name_is_plain_text() {
        const tile = createTile();
        tile.shortName = "<b>Gol</b>";
        compare(findChild(tile, "tileName").textFormat, Text.PlainText);
    }
}
