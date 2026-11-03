import QtQuick
import QtQuick.Controls.Basic
import QtTest
import FootballDSS

// Markup in user data stays text in every part of the kit that shows such data (AGENTS section 6).
TestCase {
    id: testCase

    readonly property string markup: "<b>x</b><img src=\"http://example.invalid/a.png\">"
    readonly property string opponent: "<a href='x'>Club</a>"

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
        id: weekGridComponent

        CalendarWeekGrid {
            width: 800
            height: 300
            model: [
                {
                    "date": "2026-11-02",
                    "dayName": "Monday",
                    "dateText": "02.11.2026",
                    "items": [
                        {
                            "kind": "match",
                            "itemId": 1,
                            "time": "18:00",
                            "kindText": "Match",
                            "title": testCase.opponent,
                            "details": testCase.markup,
                            "statusText": "Planned"
                        }
                    ]
                }
            ]
        }
    }

    Component {
        id: toolTipComponent

        Item {
            width: 100
            height: 40

            PlainToolTip {
                objectName: "tip"
                text: testCase.markup
                visible: true
            }
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
            { "tag": "DataTable", "component": tableComponent },
            { "tag": "CalendarWeekGrid", "component": weekGridComponent }
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

    function test_plainToolTip() {
        const host = create(toolTipComponent);
        const tip = findChild(host, "tip");
        verify(tip !== null);
        compare(tip.contentItem.textFormat, Text.PlainText);
        compare(tip.contentItem.text, testCase.markup);
    }

    function test_weekItemToolTipShowsTheNameLiterally() {
        const grid = create(weekGridComponent);
        const button = findChild(grid, "weekItem_match_1");
        verify(button !== null);
        const tip = findChild(button, "itemToolTip");
        verify(tip !== null);
        mouseMove(button, button.width / 2, button.height / 2);
        tryVerify(() => tip.opened);
        compare(tip.contentItem.textFormat, Text.PlainText);
        verify(tip.contentItem.text.indexOf(testCase.opponent) >= 0, tip.contentItem.text);
        mouseMove(grid, 0, 0);
    }

    function test_dialogTitle() {
        const dialog = createTemporaryObject(dialogComponent, testCase.parent);
        verify(dialog !== null);
        verify(expectPlain(dialog.header) > 0);
    }
}
