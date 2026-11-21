import QtQuick
import QtTest
import FootballDSS

// MatchRatingsGrid (P14): the keyboard path of the ratings (digits, Tab to the RPE and the next
// row, Enter down the same column, Esc, Delete), and read-only.
TestCase {
    id: testCase

    name: "MatchRatingsGrid"
    width: 900
    height: 400
    // Tab and the mouse skip what is not visible, and a TestCase is not by default.
    visible: true
    when: windowShown

    Component {
        id: hostComponent

        Item {
            property alias grid: ratingsGrid
            property var entered: []
            property var reverted: []
            property var digits: []
            property var cleared: []

            width: 880
            height: 380

            ListModel {
                id: rowsModel

                ListElement { playerId: 1; shirtNumber: 1; playerName: "Pierwszy"; positionCode: "GK"; started: true; minutes: 90; minutesText: "90'"; goals: 0; assists: 0; yellowCards: 0; redCards: 0; rating: 7; rpe: 5; rowError: ""; changed: false }
                ListElement { playerId: 2; shirtNumber: 2; playerName: "Drugi"; positionCode: "CB"; started: true; minutes: 60; minutesText: "60'"; goals: 1; assists: 0; yellowCards: 1; redCards: 1; rating: 0; rpe: 6; rowError: "Rate from 1 to 10"; changed: true }
                ListElement { playerId: 3; shirtNumber: 3; playerName: "Trzeci"; positionCode: "ST"; started: false; minutes: 30; minutesText: "30'"; goals: 0; assists: 1; yellowCards: 0; redCards: 0; rating: 6; rpe: 4; rowError: ""; changed: false }
            }

            MatchRatingsGrid {
                id: ratingsGrid

                anchors.fill: parent
                model: rowsModel
                onCellEntered: (row, column) => parent.entered = parent.entered.concat([[row, column]])
                onCellReverted: (row, column) => parent.reverted = parent.reverted.concat([[row, column]])
                onDigitEntered: (row, column, digit) => parent.digits = parent.digits.concat([[row, column, digit]])
                onCellCleared: (row, column) => parent.cleared = parent.cleared.concat([[row, column]])
            }
        }
    }

    function init() {
        failOnWarning(/.?/);
    }

    function createHost() {
        const host = createTemporaryObject(hostComponent, testCase);
        verify(host);
        wait(0);
        return host;
    }

    function cell(host, name) {
        const found = findChild(host.grid, name);
        verify(found, name);
        return found;
    }

    function test_digits_and_tab_walk_the_rows() {
        const host = createHost();
        host.grid.focusFirstEditable();
        verify(cell(host, "ratingCell_0").activeFocus);
        keyClick(Qt.Key_8);
        keyClick(Qt.Key_Tab);
        verify(cell(host, "matchRpeCell_0").activeFocus);
        keyClick(Qt.Key_6);
        keyClick(Qt.Key_Tab);
        verify(cell(host, "ratingCell_1").activeFocus);
        compare(host.digits, [[0, MatchRatingsModel.RatingColumn, 8], [0, MatchRatingsModel.RpeColumn, 6]]);
        compare(host.entered[0], [0, MatchRatingsModel.RatingColumn]);
        compare(host.entered[2], [1, MatchRatingsModel.RatingColumn]);
    }

    function test_enter_goes_down_the_same_column() {
        const host = createHost();
        host.grid.focusCell(0, MatchRatingsModel.RpeColumn);
        keyClick(Qt.Key_Return);
        verify(cell(host, "matchRpeCell_1").activeFocus);
        keyClick(Qt.Key_Enter);
        verify(cell(host, "matchRpeCell_2").activeFocus);
        keyClick(Qt.Key_Return); // the last row stays
        verify(cell(host, "matchRpeCell_2").activeFocus);
    }

    function test_escape_reverts_and_delete_clears() {
        const host = createHost();
        host.grid.focusCell(1, MatchRatingsModel.RatingColumn);
        keyClick(Qt.Key_Escape);
        keyClick(Qt.Key_Delete);
        compare(host.reverted, [[1, MatchRatingsModel.RatingColumn]]);
        compare(host.cleared, [[1, MatchRatingsModel.RatingColumn]]);
        verify(cell(host, "ratingRowError_1").visible);
        verify(!cell(host, "ratingRowError_0").visible);
    }

    function test_read_only_takes_the_focus_itself() {
        const host = createHost();
        host.grid.readOnly = true;
        wait(0);
        host.grid.focusFirstEditable();
        verify(host.grid.activeFocus);
        verify(!cell(host, "ratingCell_0").enabled);
        keyClick(Qt.Key_5);
        compare(host.digits, []);
    }
}
