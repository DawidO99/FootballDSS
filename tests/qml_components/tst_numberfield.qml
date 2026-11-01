import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "NumberField"
    width: 400
    height: 200
    when: windowShown

    Component {
        id: hostComponent

        Item {
            property var source: 5
            property int lowest: 0
            property int highest: 99
            property var modifications: []
            property alias field: numberField

            width: 300
            height: 44

            NumberField {
                id: numberField

                width: parent.width
                from: parent.lowest
                to: parent.highest
                value: parent.source
                onValueModified: value => parent.modifications = parent.modifications.concat([value])
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

    function init() {
        failOnWarning(/.?/);
    }

    function test_edit_does_not_assign_input_value() {
        const host = createHost();
        host.field.increase();
        compare(host.field.value, 5);
        compare(host.field.currentValue, 6);
        compare(host.field.displayText, "6");
        compare(host.modifications, [6]);
    }

    function test_source_updates_field_after_step() {
        const host = createHost();
        host.field.increase();
        host.source = 9;
        compare(host.field.value, 9);
        compare(host.field.currentValue, 9);
        compare(host.field.displayText, "9");
    }

    function test_source_updates_field_after_typing() {
        const host = createHost();
        retype(host.field, "42");
        compare(host.field.currentValue, 42);
        compare(host.field.value, 5);
        host.source = 7;
        compare(host.field.currentValue, 7);
        compare(host.field.displayText, "7");
        compare(host.modifications, [4, 42]);
    }

    function test_source_replaces_invalid_text() {
        const host = createHost();
        retype(host.field, "150");
        verify(host.field.validationError.length > 0);
        host.source = 7;
        compare(host.field.displayText, "7");
        compare(host.field.validationError, "");
    }

    function test_source_can_become_empty() {
        const host = createHost();
        host.field.increase();
        host.source = null;
        compare(host.field.currentValue, null);
        compare(host.field.displayText, "");
    }

    function test_steps_continue_from_edited_value() {
        const host = createHost();
        host.field.increase();
        host.field.increase();
        compare(host.field.currentValue, 7);
        compare(host.modifications, [6, 7]);
    }

    function test_viewmodel_echo_keeps_typed_text() {
        const host = createHost({ "lowest": 1 });
        host.field.valueModified.connect(value => host.source = value);
        retype(host.field, "007");
        compare(host.field.displayText, "007");
        compare(host.source, 7);
        compare(host.field.validationError, "");
    }

    function test_narrowing_range_shows_error_without_changing_value() {
        const host = createHost({ "source": 50 });
        host.highest = 10;
        verify(host.field.validationError.length > 0);
        compare(host.field.effectiveError, host.field.validationError);
        compare(host.field.value, 50);
        compare(host.field.currentValue, 50);
        compare(host.field.displayText, "50");
        compare(host.modifications, []);
    }

    function test_widening_range_clears_error() {
        const host = createHost({ "source": 50, "highest": 10 });
        verify(host.field.validationError.length > 0);
        host.highest = 99;
        compare(host.field.validationError, "");
        compare(host.modifications, []);
    }

    function test_raising_from_revalidates() {
        const host = createHost({ "source": 5 });
        host.lowest = 10;
        verify(host.field.validationError.length > 0);
        compare(host.field.currentValue, 5);
        host.lowest = 1;
        compare(host.field.validationError, "");
    }

    function test_widening_range_accepts_pending_text() {
        const host = createHost();
        retype(host.field, "150");
        verify(host.field.validationError.length > 0);
        compare(host.field.currentValue, 15);
        host.highest = 200;
        compare(host.field.validationError, "");
        compare(host.field.currentValue, 150);
        compare(host.modifications, [1, 15, 150]);
    }

    function test_narrowing_range_never_truncates_text() {
        const host = createHost({ "source": 100, "highest": 999 });
        host.highest = 50;
        compare(host.field.displayText, "100");
        compare(host.field.currentValue, 100);
        compare(host.modifications, []);
    }

    function test_range_change_leaves_untouched_empty_field_valid() {
        const host = createHost({ "source": null });
        host.highest = 10;
        compare(host.field.validationError, "");
    }
}
