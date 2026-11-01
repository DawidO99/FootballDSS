import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "LanguageThemeSwitch"
    width: 400
    height: 200
    visible: true
    when: windowShown

    Component {
        id: hostComponent

        FocusScope {
            property alias control: control
            property alias field: field

            width: 400
            height: 200

            PinField {
                id: field

                focus: true
            }

            LanguageThemeSwitch {
                id: control

                y: field.height + Theme.spacing.m
            }
        }
    }

    function createHost() {
        const host = createTemporaryObject(hostComponent, testCase);
        verify(host);
        waitForRendering(host);
        return host;
    }

    // What a Repeater creates is not reached by findChild() from the control.
    function languageButton(control, language) {
        const row = control.children[0];
        for (let i = 0; i < row.children.length; ++i) {
            if (row.children[i].objectName === "language_" + language)
                return row.children[i];
        }
        return null;
    }

    function init() {
        failOnWarning(/.?/);
        LanguageManager.setLanguage("en");
    }

    function cleanup() {
        LanguageManager.setLanguage("en");
        AppSettings.darkTheme = true;
    }

    function test_click_switches_the_language() {
        const host = createHost();
        const button = languageButton(host.control, "pl");
        verify(button);
        mouseClick(button);
        compare(LanguageManager.language, "pl");
        verify(button.selected);
        verify(!languageButton(host.control, "en").selected);
    }

    function test_click_toggles_the_theme() {
        const host = createHost();
        const toggle = findChild(host.control, "themeToggle");
        verify(toggle);
        AppSettings.darkTheme = true;
        mouseClick(toggle);
        compare(AppSettings.darkTheme, false);
        mouseClick(toggle);
        compare(AppSettings.darkTheme, true);
    }

    function test_click_leaves_the_focus_in_the_field() {
        const host = createHost();
        host.field.forceActiveFocus();
        keyClick(Qt.Key_1);
        mouseClick(languageButton(host.control, "pl"));
        mouseClick(findChild(host.control, "themeToggle"));
        verify(host.field.activeFocus);
        keyClick(Qt.Key_2);
        compare(host.field.text, "12");
    }

    function test_tab_reaches_them_and_enter_clicks() {
        const host = createHost();
        const button = languageButton(host.control, "pl");
        button.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(LanguageManager.language, "pl");
        verify(button.activeFocusOnTab);
    }

    function test_every_control_is_a_hit_target() {
        const host = createHost();
        for (const item of [languageButton(host.control, "pl"), languageButton(host.control, "en"), findChild(host.control, "themeToggle")]) {
            verify(item.width >= Theme.hitMin);
            verify(item.height >= Theme.hitMin);
        }
    }
}
