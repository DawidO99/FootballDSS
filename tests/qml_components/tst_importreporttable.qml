import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "ImportReportTable"
    width: 900
    height: 500
    visible: true
    when: windowShown

    Component {
        id: reportComponent

        ImportReportTable {
            width: 800
            height: 400
            model: [
                {
                    "rowNumber": 2,
                    "statusKey": "error",
                    "statusText": "!",
                    "name": "Żółkiewski",
                    "action": "",
                    "details": "abc",
                    "message": "Żółkiewski: <b>abc</b> " + "pełny komunikat ".repeat(20)
                },
                {
                    "rowNumber": 3,
                    "statusKey": "warning",
                    "statusText": "!",
                    "name": "Nowak",
                    "action": "",
                    "details": "xyz",
                    "message": "Nowak: xyz"
                }
            ]
        }
    }

    function init() {
        failOnWarning(/.?/);
    }

    function test_keyboard_selection_exposes_the_complete_plain_message() {
        const report = createTemporaryObject(reportComponent, testCase);
        verify(report);
        const rows = findChild(report, "tableRows");
        const message = findChild(report, "importSelectedMessage");
        verify(rows && message);
        report.currentIndex = -1;
        rows.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down);
        compare(report.currentIndex, 0);
        tryCompare(message, "text", report.model[0].message);
        verify(message.visible);
        compare(message.textFormat, Text.PlainText);
        compare(message.elide, Text.ElideNone);
        compare(report.Accessible.description, report.model[0].message);
        compare(message.Accessible.description, report.model[0].message);
        keyClick(Qt.Key_Down);
        compare(report.currentIndex, 1);
        tryCompare(message, "text", report.model[1].message);
        compare(report.Accessible.description, report.model[1].message);
    }

    function test_report_headers_do_not_offer_sorting() {
        const report = createTemporaryObject(reportComponent, testCase);
        verify(report);
        const rows = findChild(report, "tableRows");
        const header = findChild(report, "header_rowNumber");
        verify(rows && header);
        // Every header is disabled, so neither a click nor keyboard activation requests sorting.
        for (const column of report.columns) {
            compare(column.sortable, false);
            const item = findChild(report, "header_" + column.role);
            verify(item);
            compare(item.enabled, false);
            compare(item.focusPolicy, Qt.NoFocus);
            mouseClick(item);
        }
        compare(report.currentIndex, 0);
    }
}
