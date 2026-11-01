import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "CheckBoxField"
    width: 400
    height: 200
    when: windowShown

    Component {
        id: hostComponent

        FocusScope {
            property bool source: false
            property int toggles: 0
            property int enterCount: 0
            property alias box: checkBox

            width: 300
            height: 44
            focus: true
            Keys.onReturnPressed: enterCount += 1

            CheckBoxField {
                id: checkBox

                focus: true
                text: "Show archived"
                checked: parent.source
                onToggled: parent.toggles += 1
            }
        }
    }

    function init() {
        failOnWarning(/.?/);
    }

    function createHost() {
        const host = createTemporaryObject(hostComponent, testCase.parent);
        verify(host !== null);
        host.forceActiveFocus();
        return host;
    }

    function test_spaceToggles() {
        const host = createHost();
        keyClick(Qt.Key_Space);
        verify(host.box.checked);
        compare(host.toggles, 1);
    }

    function test_enterIsPassedOn() {
        const host = createHost();
        keyClick(Qt.Key_Return);
        verify(!host.box.checked);
        compare(host.enterCount, 1);
    }

    function test_bindingSurvivesAClick() {
        const host = createHost();
        mouseClick(host.box);
        verify(host.box.checked);
        host.source = true;
        host.source = false;
        verify(!host.box.checked);
    }

    function test_hitTargetIsLargeEnough() {
        const host = createHost();
        verify(host.box.height >= Theme.hitMin);
    }
}
