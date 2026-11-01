import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "SegmentedControl"
    width: 400
    height: 200
    when: windowShown

    Component {
        id: hostComponent

        Item {
            property var source: "a"
            property var activations: []
            property alias control: segmentedControl

            width: 300
            height: 60

            SegmentedControl {
                id: segmentedControl

                model: [
                    { "value": "a", "text": "A" },
                    { "value": "b", "text": "B" },
                    { "value": "c", "text": "C" }
                ]
                currentValue: parent.source
                onActivated: value => parent.activations = parent.activations.concat([value])
            }
        }
    }

    function createHost() {
        const host = createTemporaryObject(hostComponent, testCase);
        verify(host);
        return host;
    }

    function init() {
        failOnWarning(/.?/);
    }

    function test_user_selection_does_not_assign_input_value() {
        const host = createHost();
        host.control.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(host.control.currentIndex, 1);
        compare(host.control.currentValue, "a");
        compare(host.activations, ["b"]);
    }

    function test_source_updates_control_after_user_selection() {
        const host = createHost();
        host.control.forceActiveFocus();
        keyClick(Qt.Key_Right);
        host.source = "c";
        compare(host.control.currentValue, "c");
        compare(host.control.currentIndex, 2);
    }

    function test_keys_continue_from_user_selection() {
        const host = createHost();
        host.control.forceActiveFocus();
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Right);
        compare(host.control.currentIndex, 2);
        compare(host.activations, ["b", "c"]);
    }

    function test_touch_segments_meet_touch_hit_target() {
        const host = createHost();
        host.control.touch = true;
        waitForItemPolished(host.control);
        const segment = findChild(host.control, "segment0");
        verify(segment);
        verify(segment.height >= Theme.hitTouch, "segment height " + segment.height);
        verify(segment.width >= Theme.hitTouch, "segment width " + segment.width);
    }

    function test_normal_segments_meet_minimum_hit_target() {
        const host = createHost();
        waitForItemPolished(host.control);
        const segment = findChild(host.control, "segment0");
        verify(segment);
        verify(segment.height >= Theme.hitMin, "segment height " + segment.height);
    }
}
