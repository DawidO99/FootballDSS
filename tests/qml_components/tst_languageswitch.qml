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

    function switchLanguage() {
        LanguageManager.setLanguage(LanguageManager.language === "pl" ? "en" : "pl");
    }

    function init() {
        failOnWarning(/.?/);
        LanguageManager.setLanguage("en");
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
