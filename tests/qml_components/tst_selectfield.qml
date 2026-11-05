import QtQuick
import QtTest
import Qt.labs.qmlmodels
import FootballDSS

TestCase {
    id: testCase

    name: "SelectField"
    width: 400
    height: 400
    when: windowShown

    Component {
        id: arrayHostComponent

        Item {
            property var source: "b"
            property var activations: []
            property alias field: selectField

            width: 300
            height: 44

            SelectField {
                id: selectField

                width: parent.width
                textRole: "text"
                valueRole: "value"
                model: [
                    { "value": "a", "text": "A" },
                    { "value": "b", "text": "B" },
                    { "value": "c", "text": "C" }
                ]
                value: parent.source
                onValueActivated: value => parent.activations = parent.activations.concat([value])
            }
        }
    }

    Component {
        id: listModelHostComponent

        Item {
            property var source: "c"
            property alias field: selectField
            property alias rows: rowsModel

            width: 300
            height: 44

            ListModel {
                id: rowsModel

                ListElement {
                    value: "a"
                    text: "A"
                }
            }

            SelectField {
                id: selectField

                width: parent.width
                textRole: "text"
                valueRole: "value"
                model: rowsModel
                value: parent.source
                placeholderText: "None"
            }
        }
    }

    // TableModel resets on `rows` assignment even when the row count stays the same, which is what
    // a viewmodel's beginResetModel()/endResetModel() does; ListModel cannot do that.
    Component {
        id: resetHostComponent

        Item {
            property var source: "b"
            property alias field: selectField
            property alias rows: rowsModel

            width: 300
            height: 44

            TableModel {
                id: rowsModel

                TableModelColumn {
                    display: "value"
                    edit: "label"
                }

                rows: [
                    { "value": "a", "label": "A" },
                    { "value": "b", "label": "B" }
                ]
            }

            SelectField {
                id: selectField

                width: parent.width
                textRole: "edit"
                valueRole: "display"
                model: rowsModel
                value: parent.source
            }
        }
    }

    function createHost(component) {
        const host = createTemporaryObject(component, testCase);
        verify(host);
        return host;
    }

    function init() {
        failOnWarning(/.?/);
    }

    // Regression for review item 1.

    function test_user_pick_does_not_assign_input_value() {
        const host = createHost(arrayHostComponent);
        host.field.forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(host.field.currentValue, "c");
        compare(host.field.value, "b");
        compare(host.activations, ["c"]);
    }

    function test_source_updates_field_after_user_pick() {
        const host = createHost(arrayHostComponent);
        host.field.forceActiveFocus();
        keyClick(Qt.Key_Down);
        host.source = "a";
        compare(host.field.value, "a");
        compare(host.field.currentValue, "a");
        compare(host.field.currentIndex, 0);
    }

    function test_source_can_clear_selection() {
        const host = createHost(arrayHostComponent);
        host.source = undefined;
        compare(host.field.currentIndex, -1);
    }

    // Regression for review item 3.

    function test_selection_follows_inserted_rows() {
        const host = createHost(listModelHostComponent);
        compare(host.field.currentIndex, -1);
        compare(host.field.displayText, "None");
        host.rows.append({ "value": "b", "text": "B" });
        compare(host.field.currentIndex, -1);
        host.rows.append({ "value": "c", "text": "C" });
        tryCompare(host.field, "currentIndex", 2);
        compare(host.field.currentValue, "c");
        compare(host.field.displayText, "C");
    }

    function test_selection_follows_row_inserted_before_it() {
        const host = createHost(listModelHostComponent);
        host.source = "a";
        compare(host.field.currentIndex, 0);
        host.rows.insert(0, { "value": "z", "text": "Z" });
        tryCompare(host.field, "currentIndex", 1);
        compare(host.field.currentValue, "a");
    }

    function test_selection_survives_clear_and_refill() {
        const host = createHost(listModelHostComponent);
        host.source = "a";
        host.rows.clear();
        tryCompare(host.field, "currentIndex", -1);
        host.rows.append({ "value": "a", "text": "A" });
        tryCompare(host.field, "currentIndex", 0);
        compare(host.field.currentValue, "a");
    }

    function test_selection_follows_reset_with_same_count() {
        const host = createHost(resetHostComponent);
        compare(host.field.currentIndex, 1);
        host.rows.rows = [
            { "value": "b", "label": "B" },
            { "value": "a", "label": "A" }
        ];
        tryCompare(host.field, "currentIndex", 0);
        compare(host.field.currentValue, "b");
    }

    function test_show_value_undoes_a_pick_the_owner_did_not_accept() {
        const host = createHost(listModelHostComponent);
        host.rows.append({ "value": "b", "text": "B" });
        host.source = "b";
        tryCompare(host.field, "currentValue", "b");
        host.field.currentIndex = 0;
        host.field.activated(0);
        compare(host.field.currentValue, "a");
        // `value` never changed, so nothing would bring the field back on its own.
        host.field.showValue();
        compare(host.field.currentValue, "b");
    }

    function test_user_pick_survives_model_change() {
        const host = createHost(listModelHostComponent);
        host.source = "a";
        host.rows.append({ "value": "b", "text": "B" });
        tryCompare(host.field, "count", 2);
        host.field.forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(host.field.currentValue, "b");
        host.rows.append({ "value": "c", "text": "C" });
        tryCompare(host.field, "count", 3);
        wait(0);
        compare(host.field.currentValue, "b");
    }
}
