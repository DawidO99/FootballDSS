import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "ProfileTile"
    width: 400
    height: 400
    visible: true
    when: windowShown

    Component {
        id: tileComponent

        ProfileTile {
            property int clicks: 0

            name: "Jan Kowalski"
            roleName: "Coach"
            initials: "JK"
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

    function test_click_activates() {
        const tile = createTile();
        mouseClick(tile);
        compare(tile.clicks, 1);
    }

    function test_enter_and_space_activate() {
        const tile = createTile();
        tile.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Enter);
        keyClick(Qt.Key_Space);
        compare(tile.clicks, 3);
    }

    function test_selection_is_not_color_only() {
        const tile = createTile();
        const mark = findChild(tile, "selectedMark");
        verify(mark);
        verify(!mark.visible);
        tile.selected = true;
        verify(mark.visible);
    }

    function test_is_a_hit_target() {
        const tile = createTile();
        verify(tile.height >= Theme.hitTouch);
        verify(tile.width >= Theme.hitTouch);
    }

    function test_accessible_name_says_who() {
        const tile = createTile();
        verify(tile.Accessible.name.includes("Jan Kowalski"));
        verify(tile.Accessible.name.includes("Coach"));
    }
}
