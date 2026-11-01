import QtQuick
import QtTest
import FootballDSS

// Regression for review item 9: QQmlEngine::retranslate() re-evaluates every binding that calls
// qsTr(), so a translated array assigned to a model looks like a new model. Views keep their state.
TestCase {
    id: testCase

    name: "LanguageSwitch"
    width: 1400
    height: 800
    when: windowShown

    Component {
        id: tabsComponent

        AppTabs {
            width: 400
            height: 200
            titles: [qsTranslate("ComponentGalleryView", "Buttons & badges"), qsTranslate("ComponentGalleryView", "Form fields"), qsTranslate("ComponentGalleryView", "Data table")]

            Item {}
            Item {}
            Item {}
        }
    }

    Component {
        id: galleryComponent

        ComponentGalleryView {
            width: 1400
            height: 800
        }
    }

    Component {
        id: selectComponent

        SelectField {
            textRole: "text"
            valueRole: "value"
            model: [
                { "value": "a", "text": qsTranslate("ComponentGalleryView", "Available") },
                { "value": "b", "text": qsTranslate("ComponentGalleryView", "Caution") }
            ]
            value: "a"
        }
    }

    Component {
        id: tableComponent

        DataTable {
            width: 800
            height: 300
            model: [
                { "name": "x" }
            ]
            columns: [
                { "role": "name", "title": qsTranslate("ComponentGalleryView", "Player"), "width": 1 }
            ]
        }
    }

    Component {
        id: numberComponent

        NumberField {
            width: 300
            from: 0
            to: 99
        }
    }

    Component {
        id: dateComponent

        DateField {
            width: 300
        }
    }

    Component {
        id: textComponent

        TextInputField {
            width: 300
            validator: IntValidator {}
        }
    }

    function retype(field, text) {
        field.forceActiveFocus();
        keyClick(Qt.Key_A, Qt.ControlModifier);
        keyClick(Qt.Key_Delete);
        for (let i = 0; i < text.length; ++i)
            keyClick(text[i]);
    }

    function switchLanguage() {
        LanguageManager.setLanguage(LanguageManager.language === "pl" ? "en" : "pl");
    }

    function init() {
        failOnWarning(/.?/);
        LanguageManager.setLanguage("en");
    }

    // Regression for review item 14: validation messages follow the language; the rejected text and
    // the value stay.

    function test_number_range_message_follows_language() {
        const field = createTemporaryObject(numberComponent, testCase, { "value": 150 });
        verify(field);
        compare(field.validationError, "Enter a number from 0 to 99.");
        switchLanguage();
        verify(field.validationError !== "Enter a number from 0 to 99.");
        verify(field.validationError.includes("99"));
        compare(field.effectiveError, field.validationError);
        compare(field.displayText, "150");
        compare(field.currentValue, 150);
        switchLanguage();
        compare(field.validationError, "Enter a number from 0 to 99.");
    }

    function test_number_required_message_follows_language() {
        const field = createTemporaryObject(numberComponent, testCase, { "value": 5 });
        verify(field);
        retype(field, "");
        compare(field.validationError, "Enter a number.");
        switchLanguage();
        verify(field.validationError.length > 0);
        verify(field.validationError !== "Enter a number.");
    }

    function test_number_message_follows_limits() {
        const field = createTemporaryObject(numberComponent, testCase, { "value": 150 });
        verify(field);
        field.to = 120;
        compare(field.validationError, "Enter a number from 0 to 120.");
    }

    function test_date_range_message_follows_language() {
        const field = createTemporaryObject(dateComponent, testCase, { "date": "2026-11-01", "maximumDate": "2026-01-01" });
        verify(field);
        compare(field.validationError, "Enter a date on or before 01/01/2026.");
        switchLanguage();
        verify(field.validationError !== "Enter a date on or before 01/01/2026.");
        verify(field.validationError.includes("01.01.2026"), "the date follows the language too");
        compare(field.displayText, "01.11.2026");
        compare(field.currentDate, "2026-11-01");
    }

    function test_date_rejected_text_is_rewritten_for_the_new_language() {
        const field = createTemporaryObject(dateComponent, testCase, { "date": "2026-11-01" });
        verify(field);
        retype(field, "31/12/1899");
        compare(field.validationError, "Enter a date on or after 01/01/1900.");
        switchLanguage();
        compare(field.displayText, "31.12.1899");
        verify(field.validationError.includes("01.01.1900"), "still a range error, not a format one");
        compare(field.currentDate, "2026-11-01");
    }

    function test_date_format_message_follows_language() {
        const field = createTemporaryObject(dateComponent, testCase, { "date": "2026-11-01" });
        verify(field);
        retype(field, "garbage");
        compare(field.validationError, "Use format dd/MM/yyyy.");
        switchLanguage();
        verify(field.validationError !== "Use format dd/MM/yyyy.");
        verify(field.validationError.includes("dd.MM.yyyy"));
        compare(field.displayText, "garbage");
        compare(field.currentDate, "2026-11-01");
    }

    function test_date_required_message_follows_language() {
        const field = createTemporaryObject(dateComponent, testCase, { "date": "2026-11-01" });
        verify(field);
        retype(field, "");
        compare(field.validationError, "Enter a date (dd/MM/yyyy).");
        switchLanguage();
        verify(field.validationError !== "Enter a date (dd/MM/yyyy).");
        verify(field.validationError.includes("dd.MM.yyyy"));
    }

    function test_text_input_message_follows_language() {
        const field = createTemporaryObject(textComponent, testCase, { "text": "abc" });
        verify(field);
        compare(field.validationError, "Invalid value");
        switchLanguage();
        verify(field.validationError.length > 0);
        verify(field.validationError !== "Invalid value");
    }

    function test_tabs_keep_current_index() {
        const tabs = createTemporaryObject(tabsComponent, testCase);
        verify(tabs);
        tabs.currentIndex = 2;
        const firstTitle = tabs.titles[0];
        switchLanguage();
        compare(tabs.currentIndex, 2);
        verify(tabs.titles[0] !== firstTitle, "the Polish title is applied");
        switchLanguage();
        compare(tabs.currentIndex, 2);
    }

    function test_gallery_keeps_current_tab() {
        const gallery = createTemporaryObject(galleryComponent, testCase);
        verify(gallery);
        gallery.currentTabIndex = 3;
        switchLanguage();
        compare(gallery.currentTabIndex, 3);
    }

    function test_select_keeps_user_pick() {
        const select = createTemporaryObject(selectComponent, testCase);
        verify(select);
        select.forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(select.currentValue, "b");
        const pickedText = select.currentText;
        switchLanguage();
        tryCompare(select, "currentValue", "b");
        verify(select.currentText !== pickedText, "the Polish text is applied");
    }

    function test_table_header_keeps_focus() {
        const table = createTemporaryObject(tableComponent, testCase);
        verify(table);
        const header = findChild(table, "header_name");
        header.forceActiveFocus(Qt.TabFocusReason);
        switchLanguage();
        verify(findChild(table, "header_name") === header, "the header is not rebuilt");
        verify(header.activeFocus);
    }
}
