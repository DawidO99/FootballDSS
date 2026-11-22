import QtQuick
import QtTest
import FootballDSS

// The yes/no cells of MetricValueGrid (P17 review): "not entered" is reachable with the mouse
// through a visible clear button, and with Delete on the box.
TestCase {
    id: testCase

    name: "MetricValueGrid"
    width: 900
    height: 400
    // The mouse skips what is not visible, and a TestCase is not by default.
    visible: true
    when: windowShown

    Component {
        id: hostComponent

        Item {
            property alias grid: valueGrid
            property var cleared: []
            property var toggled: []

            width: 880
            height: 380

            ListModel {
                id: rowsModel

                ListElement {
                    shirtNumber: 1
                    playerName: "Pierwszy"
                    valueText: "1"
                    checked: true
                    hasValue: true
                    stateText: "Tak"
                    rowError: ""
                    hasError: false
                }
                ListElement {
                    shirtNumber: 2
                    playerName: "Drugi"
                    valueText: ""
                    checked: false
                    hasValue: false
                    stateText: "Nie wpisano"
                    rowError: ""
                    hasError: false
                }
            }

            MetricValueGrid {
                id: valueGrid

                anchors.fill: parent
                model: rowsModel
                boolean: true
                onValueCleared: row => parent.cleared = parent.cleared.concat([row])
                onCheckToggled: (row, checked) => parent.toggled = parent.toggled.concat([[row, checked]])
            }
        }
    }

    function createHost() {
        const host = createTemporaryObject(hostComponent, testCase);
        verify(host);
        // A click needs the rows laid out and drawn, not only created.
        waitForRendering(host);
        return host;
    }

    function item(host, name) {
        const found = findChild(host.grid, name);
        verify(found, name);
        return found;
    }

    function test_clear_button_is_visible_and_big_enough() {
        const host = createHost();
        const button = item(host, "metricClearButton_0");
        verify(button.visible);
        verify(button.enabled);
        verify(button.height >= 32, "the clear button is smaller than the minimum hit target");
        verify(button.text.length > 0, "the clear button has no tooltip");
        verify(!item(host, "metricClearButton_1").enabled, "an empty cell can be cleared");
    }

    function test_clicking_clear_reports_the_row() {
        const host = createHost();
        mouseClick(item(host, "metricClearButton_0"));
        compare(host.cleared, [0]);
        mouseClick(item(host, "metricClearButton_1"));
        compare(host.cleared, [0], "a disabled button cleared a row");
    }

    function test_delete_on_the_box_clears_and_space_ticks() {
        const host = createHost();
        const box = item(host, "metricCheckCell_1");
        box.forceActiveFocus();
        keyClick(Qt.Key_Space);
        compare(host.toggled, [[1, true]]);
        keyClick(Qt.Key_Delete);
        compare(host.cleared, [1]);
    }

    function test_read_only_grid_clears_nothing() {
        const host = createHost();
        host.grid.readOnly = true;
        verify(!item(host, "metricClearButton_0").enabled);
        verify(!item(host, "metricCheckCell_0").enabled);
    }
}
