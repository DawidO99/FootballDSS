import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "DateField"
    width: 600
    height: 600
    when: windowShown

    Component {
        id: hostComponent

        Item {
            property string source: "2026-11-01"
            property var edits: []
            property alias field: dateField

            width: 300
            height: 44

            DateField {
                id: dateField

                width: parent.width
                date: parent.source
                onDateEdited: date => parent.edits = parent.edits.concat([date])
            }
        }
    }

    function createHost(properties) {
        const host = createTemporaryObject(hostComponent, testCase, properties ?? {});
        verify(host);
        return host;
    }

    function retype(field, text) {
        field.forceActiveFocus();
        keyClick(Qt.Key_A, Qt.ControlModifier);
        keyClick(Qt.Key_Delete);
        for (let i = 0; i < text.length; ++i)
            keyClick(text[i]);
    }

    function monthGrid(field) {
        field.openCalendar();
        const grid = findChild(field, "calendarMonthGrid");
        verify(grid);
        return grid;
    }

    function init() {
        failOnWarning(/.?/);
        LanguageManager.setLanguage("pl");
    }

    // Regression for review item 1: edits do not cut the binding on `date`.

    function test_edit_does_not_assign_input_date() {
        const host = createHost();
        retype(host.field, "15.03.2026");
        compare(host.field.date, "2026-11-01");
        compare(host.field.currentDate, "2026-03-15");
        compare(host.edits, ["2026-03-15"]);
    }

    function test_source_updates_field_after_edit() {
        const host = createHost();
        retype(host.field, "15.03.2026");
        host.source = "2027-01-02";
        compare(host.field.date, "2027-01-02");
        compare(host.field.currentDate, "2027-01-02");
        compare(host.field.displayText, "02.01.2027");
    }

    function test_source_updates_field_after_calendar_pick() {
        const host = createHost();
        host.field.openCalendar();
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Return);
        compare(host.field.currentDate, "2026-11-02");
        compare(host.field.date, "2026-11-01");
        host.source = "2027-01-02";
        compare(host.field.displayText, "02.01.2027");
    }

    function test_source_replaces_invalid_text() {
        const host = createHost();
        retype(host.field, "99.99.2026");
        verify(host.field.validationError.length > 0);
        host.source = "2027-01-02";
        compare(host.field.displayText, "02.01.2027");
        compare(host.field.validationError, "");
    }

    function test_source_can_become_empty() {
        const host = createHost();
        retype(host.field, "15.03.2026");
        host.source = "";
        compare(host.field.currentDate, "");
        compare(host.field.displayText, "");
    }

    // Regression for review item 7: changing the limits validates again.

    function test_narrowing_range_shows_error_without_changing_date() {
        const host = createHost();
        host.field.minimumDate = "2027-01-01";
        verify(host.field.validationError.length > 0);
        compare(host.field.date, "2026-11-01");
        compare(host.field.currentDate, "2026-11-01");
        compare(host.field.displayText, "01.11.2026");
        compare(host.edits, []);
    }

    function test_widening_range_clears_error() {
        const host = createHost();
        host.field.maximumDate = "2026-01-01";
        verify(host.field.validationError.length > 0);
        host.field.maximumDate = "2027-01-01";
        compare(host.field.validationError, "");
        compare(host.edits, []);
    }

    function test_widening_range_accepts_pending_text() {
        const host = createHost();
        host.field.maximumDate = "2026-12-31";
        retype(host.field, "15.03.2027");
        verify(host.field.validationError.length > 0);
        compare(host.field.currentDate, "2026-11-01");
        host.field.maximumDate = "2027-12-31";
        compare(host.field.validationError, "");
        compare(host.field.currentDate, "2027-03-15");
        compare(host.edits, ["2027-03-15"]);
    }

    function test_range_change_leaves_untouched_empty_field_valid() {
        const host = createHost({ "source": "" });
        host.field.minimumDate = "2027-01-01";
        compare(host.field.validationError, "");
    }

    // Regression for review item 8: the core's supported range is the default, and the calendar
    // cannot leave the range.

    function test_default_range_matches_core() {
        const host = createHost();
        compare(host.field.minimumDate, "1900-01-01");
        compare(host.field.maximumDate, "2100-12-31");
    }

    function test_default_range_rejects_unsupported_dates() {
        const host = createHost();
        retype(host.field, "31.12.1899");
        verify(host.field.validationError.length > 0);
        compare(host.field.currentDate, "2026-11-01");
        retype(host.field, "01.01.2101");
        verify(host.field.validationError.length > 0);
        compare(host.field.currentDate, "2026-11-01");
        retype(host.field, "01.01.1900");
        compare(host.field.validationError, "");
        retype(host.field, "31.12.2100");
        compare(host.field.validationError, "");
        compare(host.field.currentDate, "2100-12-31");
    }

    function test_empty_limits_fall_back_to_supported_range() {
        const host = createHost();
        host.field.minimumDate = "";
        host.field.maximumDate = "";
        retype(host.field, "31.12.1899");
        verify(host.field.validationError.length > 0);
        retype(host.field, "01.01.2101");
        verify(host.field.validationError.length > 0);
    }

    function test_calendar_stops_at_lower_limit() {
        const host = createHost({ "source": "1900-01-01" });
        const grid = monthGrid(host.field);
        keyClick(Qt.Key_PageUp);
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Up);
        compare(grid.year, 1900);
        compare(grid.month, 0);
        keyClick(Qt.Key_Return);
        compare(host.field.currentDate, "1900-01-01");
    }

    function test_calendar_stops_at_upper_limit() {
        const host = createHost({ "source": "2100-12-31" });
        const grid = monthGrid(host.field);
        keyClick(Qt.Key_PageDown);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Down);
        compare(grid.year, 2100);
        compare(grid.month, 11);
        keyClick(Qt.Key_Return);
        compare(host.field.currentDate, "2100-12-31");
    }

    function test_calendar_month_shift_clamps_cursor_into_range() {
        const host = createHost({ "source": "2026-02-10" });
        host.field.minimumDate = "2026-01-15";
        const grid = monthGrid(host.field);
        keyClick(Qt.Key_PageUp);
        compare(grid.month, 0);
        keyClick(Qt.Key_PageUp);
        compare(grid.month, 0);
        keyClick(Qt.Key_Return);
        compare(host.field.currentDate, "2026-01-15");
    }

    function test_calendar_opens_inside_range() {
        const host = createHost({ "source": "" });
        host.field.minimumDate = "2030-05-10";
        host.field.maximumDate = "2030-06-20";
        const grid = monthGrid(host.field);
        compare(grid.year, 2030);
        compare(grid.month, 4);
    }

    function test_calendar_without_date_opens_at_today() {
        const host = createHost({ "source": "" });
        const grid = monthGrid(host.field);
        const today = new Date();
        compare(grid.year, today.getFullYear());
        compare(grid.month, today.getMonth());
    }

    // Regression for review item 6.

    function test_calendar_days_meet_minimum_hit_target() {
        const host = createHost();
        const grid = monthGrid(host.field);
        waitForItemPolished(grid);
        const cell = grid.contentItem.children[0];
        verify(cell.width >= Theme.hitMin, "day width " + cell.width);
        verify(cell.height >= Theme.hitMin, "day height " + cell.height);
    }

    // 29 February: leap years, century years that are not leap years, and the year 2000.

    function test_february_29_data() {
        return [
            { "tag": "2024 is a leap year", "year": "2024", "valid": true },
            { "tag": "2028 is a leap year", "year": "2028", "valid": true },
            { "tag": "2000 is a leap year", "year": "2000", "valid": true },
            { "tag": "2023 is not", "year": "2023", "valid": false },
            { "tag": "2026 is not", "year": "2026", "valid": false },
            { "tag": "1900 is not", "year": "1900", "valid": false },
            { "tag": "2100 is not", "year": "2100", "valid": false }
        ];
    }

    function test_february_29(data) {
        const languages = [
            { "language": "pl", "text": "29.02." + data.year },
            { "language": "en", "text": "29/02/" + data.year }
        ];
        for (const entry of languages) {
            LanguageManager.setLanguage(entry.language);
            const host = createHost();
            retype(host.field, entry.text);
            if (data.valid) {
                compare(host.field.validationError, "", entry.text);
                compare(host.field.currentDate, data.year + "-02-29", entry.text);
                compare(host.edits, [data.year + "-02-29"], entry.text);
            } else {
                verify(host.field.validationError.length > 0, entry.text);
                compare(host.field.currentDate, "2026-11-01", entry.text);
                compare(host.edits, [], entry.text);
            }
        }
    }

    function test_february_29_iso_source_data() {
        return test_february_29_data();
    }

    function test_february_29_iso_source(data) {
        const host = createHost({ "source": data.year + "-02-29" });
        compare(host.field.displayText, data.valid ? "29.02." + data.year : "");
    }
}
