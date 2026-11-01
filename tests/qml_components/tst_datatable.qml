import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "DataTable"
    width: 900
    height: 400
    // TestCase is invisible by default and the Tab chain skips invisible items.
    visible: true
    when: windowShown

    Component {
        id: tableComponent

        DataTable {
            width: 800
            height: 300
            model: [
                { "a": 1, "b": 2, "c": 3 },
                { "a": 4, "b": 5, "c": 6 }
            ]
            columns: [
                { "role": "a", "title": "A", "width": 0.5 },
                { "role": "b", "title": "B", "width": 0.5 },
                { "role": "c", "title": "C", "width": 100, "sortable": false }
            ]
        }
    }

    SignalSpy {
        id: sortSpy

        signalName: "sortRequested"
    }

    function createTable(properties) {
        const table = createTemporaryObject(tableComponent, testCase, properties ?? {});
        verify(table);
        sortSpy.clear();
        sortSpy.target = table;
        return table;
    }

    function sum(widths) {
        return widths.reduce((total, width) => total + width, 0);
    }

    function init() {
        failOnWarning(/.?/);
    }

    // Regression for review item 4.

    function test_pixel_columns_keep_their_width_when_they_fit() {
        const widths = createTable().computeColumnWidths([
            { "width": 100 },
            { "width": 0.75 },
            { "width": 0.25 }
        ], 500);
        compare(widths, [100, 300, 100]);
    }

    function test_pixel_columns_shrink_proportionally() {
        const widths = createTable().computeColumnWidths([
            { "width": 300 },
            { "width": 100 }
        ], 200);
        compare(widths, [150, 50]);
    }

    function test_pixel_columns_shrink_to_leave_fraction_minimum() {
        const minimum = Theme.spacing.xxl * 2;
        const widths = createTable().computeColumnWidths([
            { "width": 300 },
            { "width": 100 },
            { "width": 1 }
        ], 200 + minimum);
        compare(widths, [150, 50, minimum]);
    }

    function test_fraction_columns_respect_minimum() {
        const minimum = Theme.spacing.xxl * 2;
        const widths = createTable().computeColumnWidths([
            { "width": 0.95 },
            { "width": 0.05 }
        ], 400);
        compare(widths[1], minimum);
        compare(widths[0], 400 - minimum);
    }

    function test_columns_always_fit_when_minimums_do_not() {
        const minimum = Theme.spacing.xxl * 2;
        const widths = createTable().computeColumnWidths([
            { "width": 100 },
            { "width": 0.5 },
            { "width": 0.5 }
        ], minimum);
        fuzzyCompare(sum(widths), minimum, 0.001);
    }

    function test_live_columns_fill_table_width() {
        const table = createTable();
        fuzzyCompare(sum(table.columnWidths), table.width - 2 * Theme.borderWidth, 0.001);
        table.width = 100;
        fuzzyCompare(sum(table.columnWidths), table.width - 2 * Theme.borderWidth, 0.001);
    }

    // Regression for review item 5.

    function test_sortable_headers_are_in_tab_order() {
        // macOS can limit Tab to text fields and lists (a system setting); Qt then skips buttons.
        if (Qt.styleHints.tabFocusBehavior !== Qt.TabFocusAllControls)
            skip("the platform does not give buttons Tab focus");
        const table = createTable();
        const first = findChild(table, "header_a");
        const second = findChild(table, "header_b");
        const notSortable = findChild(table, "header_c");
        verify(first && second && notSortable);
        first.forceActiveFocus(Qt.TabFocusReason);
        verify(first.activeFocus);
        keyClick(Qt.Key_Tab);
        verify(second.activeFocus);
        keyClick(Qt.Key_Tab);
        verify(!notSortable.activeFocus);
        verify(findChild(table, "tableRows").activeFocus);
    }

    function test_enter_and_space_request_sort_data() {
        return [
            { "tag": "return", "key": Qt.Key_Return },
            { "tag": "enter", "key": Qt.Key_Enter },
            { "tag": "space", "key": Qt.Key_Space }
        ];
    }

    function test_enter_and_space_request_sort(data) {
        const table = createTable();
        const header = findChild(table, "header_a");
        header.forceActiveFocus(Qt.TabFocusReason);
        keyClick(data.key);
        compare(sortSpy.count, 1);
        compare(sortSpy.signalArguments[0][0], "a");
        compare(sortSpy.signalArguments[0][1], true);
        verify(header.activeFocus);
    }

    function test_focused_header_shows_focus_ring() {
        const table = createTable();
        const header = findChild(table, "header_a");
        compare(header.background.border.width, 0);
        header.forceActiveFocus(Qt.TabFocusReason);
        compare(header.background.border.width, Theme.focusRingWidth);
    }

    // Regression for review item 6.

    function test_touch_header_meets_touch_hit_target() {
        const table = createTable({ "touch": true });
        waitForItemPolished(table);
        const header = findChild(table, "header_a");
        verify(header.height >= Theme.hitTouch, "header height " + header.height);
    }
}
