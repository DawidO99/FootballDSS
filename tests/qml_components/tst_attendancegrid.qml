import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "AttendanceGrid"
    width: 900
    height: 400
    // Tab and the mouse skip what is not visible, and a TestCase is not by default.
    visible: true
    when: windowShown

    Component {
        id: hostComponent

        Item {
            property alias grid: attendanceGrid
            property alias rows: rowsModel
            property var entered: []
            property var reverted: []
            property var digits: []
            property var cleared: []
            property var minutes: []
            property var statuses: []

            width: 880
            height: 380

            ListModel {
                id: rowsModel

                ListElement {
                    shirtNumber: 1
                    playerName: "Pierwszy"
                    status: "present"
                    rpe: 5
                    minutes: 90
                    load: 450
                    cellsEnabled: true
                    rowError: ""
                    minutesHasError: false
                }
                ListElement {
                    shirtNumber: 2
                    playerName: "Kontuzjowany"
                    status: "injured"
                    rpe: 0
                    minutes: 0
                    load: 0
                    cellsEnabled: false
                    rowError: ""
                    minutesHasError: false
                }
                ListElement {
                    shirtNumber: 3
                    playerName: "Trzeci"
                    status: "present"
                    rpe: 7
                    minutes: 60
                    load: 420
                    cellsEnabled: true
                    rowError: ""
                    minutesHasError: false
                }
            }

            AttendanceGrid {
                id: attendanceGrid

                anchors.fill: parent
                model: rowsModel
                statusOptions: [
                    { "value": "present", "text": "Present" },
                    { "value": "absent", "text": "Absent" },
                    { "value": "excused", "text": "Excused" },
                    { "value": "injured", "text": "Injured" }
                ]
                onCellEntered: (row, column) => parent.entered = parent.entered.concat([[row, column]])
                onCellReverted: (row, column) => parent.reverted = parent.reverted.concat([[row, column]])
                onDigitEntered: (row, digit) => parent.digits = parent.digits.concat([[row, digit]])
                onRpeCleared: row => parent.cleared = parent.cleared.concat([row])
                onMinutesEdited: (row, text) => parent.minutes = parent.minutes.concat([[row, text]])
                onStatusPicked: (row, status) => parent.statuses = parent.statuses.concat([[row, status]])
            }
        }
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

    function test_status_width_comes_from_its_content() {
        const host = createHost();
        verify(host.grid.statusWidth > cell(host, "statusCell_0").implicitWidth - 1);
        verify(cell(host, "statusCell_0").width <= host.grid.statusWidth);
    }

    function test_first_editable_is_the_first_enabled_rpe() {
        const host = createHost();
        host.rows.setProperty(0, "cellsEnabled", false);
        wait(0);
        host.grid.focusFirstEditable();
        verify(cell(host, "rpeCell_2").activeFocus);
    }

    function test_without_any_enabled_rpe_the_status_gets_the_focus() {
        const host = createHost();
        host.rows.setProperty(0, "cellsEnabled", false);
        host.rows.setProperty(2, "cellsEnabled", false);
        wait(0);
        host.grid.focusFirstEditable();
        verify(cell(host, "statusCell_0").activeFocus);
    }

    function test_tab_walks_through_the_columns_past_what_is_disabled() {
        const host = createHost();
        host.grid.focusCell(0, TrainingSessionViewModel.StatusColumn);
        verify(cell(host, "statusCell_0").activeFocus);
        keyClick(Qt.Key_Tab);
        verify(cell(host, "rpeCell_0").activeFocus);
        keyClick(Qt.Key_Tab);
        verify(cell(host, "minutesCell_0").activeFocus);
        keyClick(Qt.Key_Tab);
        verify(cell(host, "statusCell_1").activeFocus);
        // RPE and minutes of the injured player are disabled.
        keyClick(Qt.Key_Tab);
        verify(cell(host, "statusCell_2").activeFocus);
    }

    function test_enter_goes_down_past_what_is_disabled() {
        const host = createHost();
        host.grid.focusCell(0, TrainingSessionViewModel.RpeColumn);
        keyClick(Qt.Key_Return);
        verify(cell(host, "rpeCell_2").activeFocus);
        // The last row: the focus stays.
        keyClick(Qt.Key_Return);
        verify(cell(host, "rpeCell_2").activeFocus);

        host.grid.focusCell(0, TrainingSessionViewModel.MinutesColumn);
        keyClick(Qt.Key_Enter);
        verify(cell(host, "minutesCell_2").activeFocus);

        // A status can always be changed, so no row is skipped.
        host.grid.focusCell(0, TrainingSessionViewModel.StatusColumn);
        keyClick(Qt.Key_Return);
        verify(cell(host, "statusCell_1").activeFocus);
    }

    function test_entering_a_cell_says_which() {
        const host = createHost();
        host.grid.focusCell(2, TrainingSessionViewModel.MinutesColumn);
        compare(host.entered[host.entered.length - 1], [2, TrainingSessionViewModel.MinutesColumn]);
        host.grid.focusCell(2, TrainingSessionViewModel.StatusColumn);
        compare(host.entered[host.entered.length - 1], [2, TrainingSessionViewModel.StatusColumn]);
    }

    function test_digits_and_clearing_in_the_rpe_cell() {
        const host = createHost();
        host.grid.focusCell(0, TrainingSessionViewModel.RpeColumn);
        keyClick(Qt.Key_7);
        keyClick(Qt.Key_0);
        compare(host.digits, [[0, 7], [0, 0]]);
        keyClick(Qt.Key_Delete);
        keyClick(Qt.Key_Backspace);
        compare(host.cleared, [0, 0]);
        // Ctrl+1 is no digit, and the letters are nothing at all.
        keyClick(Qt.Key_1, Qt.ControlModifier);
        keyClick(Qt.Key_A);
        compare(host.digits.length, 2);
    }

    function test_esc_reverts_each_kind_of_cell() {
        const host = createHost();
        host.grid.focusCell(0, TrainingSessionViewModel.RpeColumn);
        keyClick(Qt.Key_Escape);
        host.grid.focusCell(0, TrainingSessionViewModel.MinutesColumn);
        keyClick(Qt.Key_Escape);
        host.grid.focusCell(0, TrainingSessionViewModel.StatusColumn);
        keyClick(Qt.Key_Escape);
        compare(host.reverted, [[0, TrainingSessionViewModel.RpeColumn], [0, TrainingSessionViewModel.MinutesColumn], [0, TrainingSessionViewModel.StatusColumn]]);
    }

    function test_minutes_go_out_as_typed_and_replace_what_was_there() {
        const host = createHost();
        host.grid.focusCell(0, TrainingSessionViewModel.MinutesColumn);
        keyClick(Qt.Key_4);
        compare(host.minutes[0], [0, "4"]);
        compare(cell(host, "minutesCell_0").displayText, "4");
        keyClick(Qt.Key_5);
        compare(host.minutes[1], [0, "45"]);
        // Letters do not get in, and neither does a fourth digit.
        keyClick(Qt.Key_A);
        compare(cell(host, "minutesCell_0").displayText, "45");
        keyClick(Qt.Key_1);
        keyClick(Qt.Key_1);
        compare(cell(host, "minutesCell_0").displayText, "451");
    }

    function test_empty_minutes_stay_empty_while_typing_and_show_the_value_after() {
        const host = createHost();
        host.grid.focusCell(0, TrainingSessionViewModel.MinutesColumn);
        keyClick(Qt.Key_Backspace);
        compare(host.minutes[0], [0, ""]);
        compare(cell(host, "minutesCell_0").displayText, "");
        keyClick(Qt.Key_Tab);
        compare(cell(host, "minutesCell_0").displayText, "90");
    }

    function test_esc_shows_the_value_again_in_minutes() {
        const host = createHost();
        host.grid.focusCell(0, TrainingSessionViewModel.MinutesColumn);
        keyClick(Qt.Key_4);
        keyClick(Qt.Key_Escape);
        compare(cell(host, "minutesCell_0").displayText, "90");
    }

    function test_status_is_picked_with_the_keys() {
        const host = createHost();
        host.grid.focusCell(0, TrainingSessionViewModel.StatusColumn);
        keyClick(Qt.Key_Right);
        compare(host.statuses, [[0, "absent"]]);
    }

    function test_a_click_focuses_the_rpe_cell() {
        const host = createHost();
        const target = cell(host, "rpeCell_2");
        mouseClick(target);
        verify(target.activeFocus);
    }

    function test_read_only_disables_everything_and_the_grid_takes_the_focus() {
        const host = createHost();
        host.grid.readOnly = true;
        host.rows.setProperty(0, "cellsEnabled", false);
        host.rows.setProperty(2, "cellsEnabled", false);
        wait(0);
        verify(!cell(host, "statusCell_0").enabled);
        verify(!cell(host, "rpeCell_0").enabled);
        verify(!cell(host, "minutesCell_0").enabled);
        host.grid.focusFirstEditable();
        verify(host.grid.activeFocus);
    }

    function test_row_error_is_shown_in_the_row_as_plain_text() {
        const host = createHost();
        const label = cell(host, "rowError_1");
        verify(!label.visible);
        host.rows.setProperty(1, "rowError", "<b>wrong</b>");
        tryVerify(() => label.visible);
        compare(label.textFormat, Text.PlainText);
    }

    function test_page_keys_scroll() {
        const host = createHost();
        for (let i = 0; i < 30; ++i)
            host.rows.append({
                "shirtNumber": 10 + i,
                "playerName": "Kolejny",
                "status": "present",
                "rpe": 1,
                "minutes": 90,
                "load": 90,
                "cellsEnabled": true,
                "rowError": "",
                "minutesHasError": false
            });
        wait(0);
        host.grid.focusCell(0, TrainingSessionViewModel.RpeColumn);
        const page = findChild(host.grid, "attendanceRows");
        compare(page.contentY, 0);
        keyClick(Qt.Key_PageDown);
        verify(page.contentY > 0);
        keyClick(Qt.Key_PageUp);
        compare(page.contentY, 0);
    }

    function test_focus_scrolls_the_last_row_into_view() {
        const host = createHost();
        for (let i = 0; i < 30; ++i)
            host.rows.append({
                "shirtNumber": 10 + i,
                "playerName": "Kolejny",
                "status": "present",
                "rpe": 1,
                "minutes": 90,
                "load": 90,
                "cellsEnabled": true,
                "rowError": "",
                "minutesHasError": false
            });
        wait(0);
        host.grid.focusCell(host.grid.count - 1, TrainingSessionViewModel.RpeColumn);
        const page = findChild(host.grid, "attendanceRows");
        verify(page.contentY > 0);
    }
}
