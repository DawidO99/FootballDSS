import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "TimeField"
    width: 400
    height: 200
    visible: true
    when: windowShown

    Component {
        id: hostComponent

        Item {
            property string source
            property var edits: []
            property alias field: timeField

            width: 300
            height: 44

            TimeField {
                id: timeField

                width: parent.width
                time: parent.source
                onTimeEdited: time => parent.edits = parent.edits.concat([time])
            }

            TextInputField {
                id: nextField

                objectName: "nextField"
                y: 60
                width: parent.width
            }
        }
    }

    function createHost(properties) {
        const host = createTemporaryObject(hostComponent, testCase, properties ?? {});
        verify(host);
        return host;
    }

    function type(field, text) {
        field.forceActiveFocus();
        for (let i = 0; i < text.length; ++i)
            keyClick(text[i]);
    }

    function init() {
        failOnWarning(/.?/);
    }

    function test_shows_the_time_it_is_given() {
        const host = createHost({ "source": "18:30" });
        compare(host.field.text, "18:30");
        compare(host.field.currentTime, "18:30");
        compare(host.field.hasInvalidText, false);
    }

    function test_empty_is_valid() {
        const host = createHost();
        compare(host.field.text, "");
        compare(host.field.currentTime, "");
        compare(host.field.hasInvalidText, false);
        compare(host.field.effectiveError, "");
    }

    function test_every_way_of_typing_a_time_gives_the_same_form_data() {
        return [
            { "tag": "with a colon", "typed": "18:30", "time": "18:30" },
            { "tag": "with a dot", "typed": "18.30", "time": "18:30" },
            { "tag": "digits only", "typed": "1830", "time": "18:30" },
            { "tag": "one digit of hours", "typed": "9:05", "time": "09:05" },
            { "tag": "three digits", "typed": "905", "time": "09:05" },
            { "tag": "midnight", "typed": "000", "time": "00:00" },
            { "tag": "the last minute", "typed": "23:59", "time": "23:59" }
        ];
    }

    function test_every_way_of_typing_a_time_gives_the_same_form(data) {
        const host = createHost();
        type(host.field, data.typed);
        compare(host.field.currentTime, data.time);
        compare(host.edits[host.edits.length - 1], data.time);
    }

    function test_the_text_becomes_the_time_when_left() {
        const host = createHost();
        type(host.field, "905");
        host.children[1].forceActiveFocus();
        compare(host.field.text, "09:05");
    }

    function test_unfinished_text_is_no_time_and_keeps_save_off() {
        const host = createHost({ "source": "18:30" });
        host.field.forceActiveFocus();
        host.field.selectAll();
        keyClick("1");
        compare(host.field.currentTime, "");
        compare(host.field.hasInvalidText, true);
        compare(host.edits[host.edits.length - 1], "");
        // The message waits until the field is left.
        compare(host.field.validationError, "");
        host.children[1].forceActiveFocus();
        verify(host.field.validationError.length > 0);
        compare(host.field.text, "1");
    }

    function test_an_impossible_time_cannot_be_typed() {
        const host = createHost();
        type(host.field, "25:");
        compare(host.field.currentTime, "");
        compare(host.field.text, "25");
        host.field.clear();
        type(host.field, "18:6");
        compare(host.field.text, "18:");
    }

    function test_clearing_the_field_clears_the_time() {
        const host = createHost({ "source": "18:30" });
        host.field.forceActiveFocus();
        host.field.selectAll();
        keyClick(Qt.Key_Delete);
        compare(host.field.currentTime, "");
        compare(host.field.hasInvalidText, false);
        compare(host.edits[host.edits.length - 1], "");
    }

    function test_typing_does_not_lose_text_when_the_viewmodel_echoes_it() {
        const host = createHost({ "source": "18:30" });
        host.field.timeEdited.connect(time => host.source = time);
        host.field.forceActiveFocus();
        host.field.selectAll();
        keyClick("9");
        // The viewmodel now holds nothing, and the text being typed stays.
        compare(host.source, "");
        compare(host.field.text, "9");
        keyClick(":");
        keyClick("1");
        keyClick("5");
        compare(host.source, "09:15");
        compare(host.field.text, "9:15");
    }

    function test_a_new_time_from_outside_replaces_the_text() {
        const host = createHost({ "source": "18:30" });
        host.source = "07:00";
        compare(host.field.text, "07:00");
    }

    function test_showTime_drops_what_was_typed() {
        const host = createHost();
        type(host.field, "1");
        host.field.showTime();
        compare(host.field.text, "");
        compare(host.field.hasInvalidText, false);
    }
}
