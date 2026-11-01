import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "MultiSelectField"
    width: 400
    height: 500
    when: windowShown

    Component {
        id: hostComponent

        FocusScope {
            property var source: ["b"]
            property var edits: []
            property int enterCount: 0
            property alias field: multiSelect

            width: 300
            height: 44
            focus: true
            Keys.onReturnPressed: enterCount += 1

            MultiSelectField {
                id: multiSelect

                width: parent.width
                focus: true
                model: [
                    { "value": "a", "text": "A" },
                    { "value": "b", "text": "B" },
                    { "value": "c", "text": "C" }
                ]
                values: parent.source
                placeholderText: "None"
                onValuesEdited: values => parent.edits = parent.edits.concat([values])
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
        host.field.forceActiveFocus();
        tryVerify(() => host.field.activeFocus);
        return host;
    }

    function test_showsTheValuesInTheOrderOfTheModel() {
        const host = createHost();
        compare(host.field.displayText, "B");
        host.source = ["c", "a"];
        compare(host.field.displayText, "A, C");
    }

    function test_nothingSelected() {
        const host = createHost();
        host.source = [];
        compare(host.field.displayText, "");
        compare(host.field.currentValues.length, 0);
    }

    function test_spaceOpensAndTogglesEscCloses() {
        const host = createHost();
        keyClick(Qt.Key_Space);
        tryVerify(() => host.field.popupOpen);

        // The first row is current: A goes on, then Down to B, which goes off.
        keyClick(Qt.Key_Space);
        compare(host.field.currentValues, ["a", "b"]);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Space);
        compare(host.field.currentValues, ["a"]);
        compare(host.edits.length, 2);
        compare(host.edits[1], ["a"]);

        keyClick(Qt.Key_Escape);
        tryVerify(() => !host.field.popupOpen);
        tryVerify(() => host.field.activeFocus);
        compare(host.field.currentValues, ["a"]);
    }

    function test_downOpens() {
        const host = createHost();
        keyClick(Qt.Key_Down);
        tryVerify(() => host.field.popupOpen);
    }

    function test_enterInTheListClosesItAndGoesNoFurther() {
        const host = createHost();
        keyClick(Qt.Key_Space);
        tryVerify(() => host.field.popupOpen);
        keyClick(Qt.Key_Return);
        tryVerify(() => !host.field.popupOpen);
        compare(host.enterCount, 0);
    }

    function test_enterOnTheClosedFieldIsPassedOn() {
        const host = createHost();
        keyClick(Qt.Key_Return);
        compare(host.enterCount, 1);
        verify(!host.field.popupOpen);
    }

    function test_bindingSurvivesAnEdit() {
        const host = createHost();
        host.field.toggle("a");
        compare(host.field.currentValues, ["a", "b"]);
        // The field never assigns `values`, so the outside still decides.
        compare(host.field.values, ["b"]);
        host.source = ["c"];
        compare(host.field.currentValues, ["c"]);
    }

    function test_valuesNoLongerInTheModelDropOutOfTheNextEdit() {
        const host = createHost();
        host.source = ["x", "b"];
        compare(host.field.displayText, "B");
        host.field.toggle("a");
        compare(host.field.currentValues, ["a", "b"]);
    }
}
