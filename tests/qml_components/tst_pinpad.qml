import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "PinPad"
    width: 400
    height: 400
    visible: true
    when: windowShown

    Component {
        id: hostComponent

        FocusScope {
            property string typed
            property int backspaces: 0
            property int clears: 0
            property alias pad: pad
            property alias field: field

            width: 400
            height: 400

            PinField {
                id: field

                focus: true
            }

            PinPad {
                id: pad

                y: field.height + Theme.spacing.m
                onDigitPressed: digit => parent.typed += digit
                onBackspacePressed: parent.backspaces += 1
                onClearPressed: parent.clears += 1
            }
        }
    }

    function createHost() {
        const host = createTemporaryObject(hostComponent, testCase);
        verify(host);
        waitForRendering(host);
        return host;
    }

    // Every digit key by its text, in the order of the grid.
    function digitKeys(pad) {
        const keys = [];
        for (let i = 0; i < pad.children.length; ++i) {
            const child = pad.children[i];
            if (child.objectName === "" && child.text !== undefined)
                keys.push(child);
        }
        return keys;
    }

    function init() {
        failOnWarning(/.?/);
    }

    function test_has_all_ten_digits() {
        const host = createHost();
        const texts = digitKeys(host.pad).map(key => key.text).sort();
        compare(texts, ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9"]);
    }

    function test_clicks_report_the_digit() {
        const host = createHost();
        const keys = digitKeys(host.pad);
        mouseClick(keys[0]);
        mouseClick(keys[keys.length - 1]);
        compare(host.typed, keys[0].text + keys[keys.length - 1].text);
    }

    function test_clear_and_backspace_keys() {
        const host = createHost();
        mouseClick(findChild(host.pad, "clearKey"));
        compare(host.clears, 1);
        compare(host.backspaces, 0);
        mouseClick(findChild(host.pad, "backspaceKey"));
        compare(host.backspaces, 1);
        compare(host.clears, 1);
    }

    function test_every_key_is_a_touch_target() {
        const host = createHost();
        const keys = digitKeys(host.pad);
        keys.push(findChild(host.pad, "clearKey"), findChild(host.pad, "backspaceKey"));
        compare(keys.length, 12);
        for (const key of keys) {
            verify(key.width >= Theme.hitTouch, "width of " + key.text);
            verify(key.height >= Theme.hitTouch, "height of " + key.text);
        }
    }

    function test_keys_leave_the_focus_in_the_field() {
        const host = createHost();
        host.field.forceActiveFocus();
        mouseClick(digitKeys(host.pad)[0]);
        mouseClick(findChild(host.pad, "clearKey"));
        verify(host.field.activeFocus);
    }

    function test_disabled_pad_reports_nothing() {
        const host = createHost();
        host.pad.enabled = false;
        mouseClick(digitKeys(host.pad)[0]);
        mouseClick(findChild(host.pad, "clearKey"));
        compare(host.typed, "");
        compare(host.clears, 0);
    }

    function test_pin_field_takes_only_digits_up_to_eight() {
        const host = createHost();
        host.field.forceActiveFocus();
        for (const key of [Qt.Key_1, Qt.Key_A, Qt.Key_2, Qt.Key_Space])
            keyClick(key);
        compare(host.field.text, "12");
        for (let i = 0; i < 8; ++i)
            keyClick(Qt.Key_3);
        compare(host.field.text.length, 8);
        compare(host.field.effectiveError, "");
    }

    function test_pin_field_masks_the_text() {
        const host = createHost();
        host.field.text = "1234";
        verify(host.field.displayText !== "1234");
    }
}
