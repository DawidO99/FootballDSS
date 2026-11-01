import QtQuick
import QtQuick.Controls.Basic
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "TextAreaField"
    width: 400
    height: 400
    when: windowShown

    Component {
        id: hostComponent

        FocusScope {
            property int acceptedCount: 0
            property alias field: area
            property alias before: firstInput
            property alias after: lastInput

            width: 300
            height: 300
            focus: true

            Column {
                anchors.fill: parent

                TextField {
                    id: firstInput

                    width: parent.width
                }

                TextAreaField {
                    id: area

                    width: parent.width
                    onAccepted: parent.parent.acceptedCount += 1
                }

                TextField {
                    id: lastInput

                    width: parent.width
                }
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
        host.field.forceInputFocus();
        return host;
    }

    function test_enterAcceptsAndAddsNothing() {
        const host = createHost();
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_Return);
        compare(host.acceptedCount, 1);
        compare(host.field.text, "a");
        keyClick(Qt.Key_Enter);
        compare(host.acceptedCount, 2);
        compare(host.field.text, "a");
    }

    function test_shiftEnterBreaksTheLineWithAPlainNewLine() {
        const host = createHost();
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        keyClick(Qt.Key_B);
        compare(host.field.text, "a\nb");
        compare(host.acceptedCount, 0);
    }

    function test_shiftEnterReplacesTheSelection() {
        const host = createHost();
        host.field.text = "abc";
        keySequence("Ctrl+A");
        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        compare(host.field.text, "\n");
    }

    function test_tabLeavesTheFieldInBothDirections() {
        const host = createHost();
        keyClick(Qt.Key_Tab);
        verify(host.after.activeFocus);
        compare(host.field.text, "");

        host.field.forceInputFocus();
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
        verify(host.before.activeFocus);
        compare(host.field.text, "");
    }
}
