import QtQuick
import QtQuick.Controls.Basic
import QtTest
import FootballDSS

// Markup in user data stays text in every part of the kit that shows such data (AGENTS section 6).
TestCase {
    id: testCase

    readonly property string markup: "<b>x</b><img src=\"http://example.invalid/a.png\">"

    name: "PlainText"
    width: 800
    height: 600
    when: windowShown

    Component {
        id: pageHeaderComponent

        PageHeader {
            title: testCase.markup
        }
    }

    Component {
        id: cardComponent

        Card {
            title: testCase.markup
        }
    }

    Component {
        id: profileTileComponent

        ProfileTile {
            name: testCase.markup
            roleName: "Coach"
            initials: testCase.markup
        }
    }

    Component {
        id: checkBoxComponent

        CheckBoxField {
            text: testCase.markup
        }
    }

    Component {
        id: multiSelectComponent

        MultiSelectField {
            model: [
                { "value": "a", "text": testCase.markup }
            ]
            values: ["a"]
        }
    }

    Component {
        id: textAreaComponent

        TextAreaField {
            text: testCase.markup
        }
    }

    Component {
        id: selectComponent

        SelectField {
            textRole: "text"
            valueRole: "value"
            model: [
                { "value": "a", "text": testCase.markup }
            ]
            value: "a"
        }
    }

    Component {
        id: tableComponent

        DataTable {
            width: 400
            height: 200
            columns: [
                { "role": "name", "title": "Name", "width": 1 }
            ]
            model: [
                { "name": testCase.markup }
            ]
        }
    }

    Component {
        id: toastComponent

        ToastHost {
            width: 400
        }
    }

    Component {
        id: dialogComponent

        AppDialog {
            title: testCase.markup
        }
    }

    function init() {
        failOnWarning(/.?/);
    }

    // Every item under `item` that holds the markup, checked to show it as plain text.
    function expectPlain(item) {
        let found = 0;
        if (typeof item.text === "string" && item.text.indexOf(testCase.markup) >= 0 && item.textFormat !== undefined) {
            compare(item.textFormat, Text.PlainText, "rich text in " + item);
            ++found;
        }
        for (let i = 0; i < item.children.length; ++i)
            found += expectPlain(item.children[i]);
        return found;
    }

    function create(component) {
        const item = createTemporaryObject(component, testCase.parent);
        verify(item !== null);
        waitForRendering(item);
        return item;
    }

    function test_kit_data() {
        return [
            { "tag": "PageHeader", "component": pageHeaderComponent },
            { "tag": "Card", "component": cardComponent },
            { "tag": "ProfileTile", "component": profileTileComponent },
            { "tag": "CheckBoxField", "component": checkBoxComponent },
            { "tag": "MultiSelectField", "component": multiSelectComponent },
            { "tag": "TextAreaField", "component": textAreaComponent },
            { "tag": "SelectField", "component": selectComponent },
            { "tag": "DataTable", "component": tableComponent }
        ];
    }

    function test_kit(data) {
        verify(expectPlain(create(data.component)) > 0);
    }

    function test_openListOfASelectField() {
        const field = create(selectComponent);
        field.popup.open();
        tryVerify(() => field.popup.opened);
        verify(expectPlain(field.popup.contentItem) > 0);
        field.popup.close();
    }

    function test_openListOfAMultiSelectField() {
        const field = create(multiSelectComponent);
        field.openPopup();
        tryVerify(() => field.popupOpen);
        verify(expectPlain(findChild(field, "multiSelectList")) > 0);
        field.closePopup();
    }

    function test_toast() {
        const host = create(toastComponent);
        Toasts.info(testCase.markup);
        verify(expectPlain(host) > 0);
        Toasts.dismiss();
    }

    function test_dialogTitle() {
        const dialog = createTemporaryObject(dialogComponent, testCase.parent);
        verify(dialog !== null);
        verify(expectPlain(dialog.header) > 0);
    }
}
