import QtQuick
import QtQuick.Controls.Basic
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "SuggestionField"
    width: 400
    height: 400
    visible: true
    when: windowShown

    Component {
        id: hostComponent

        FocusScope {
            property string source
            property var variants: ["Liga okręgowa", "Puchar Polski", "Sparing", "Łódzka liga"]
            property int returnsSeen: 0
            property int escapesSeen: 0
            property alias field: suggestionField

            width: 300
            height: 300
            focus: true
            Keys.onReturnPressed: returnsSeen += 1
            Keys.onEscapePressed: escapesSeen += 1

            SuggestionField {
                id: suggestionField

                width: parent.width
                text: parent.source
                suggestions: parent.variants
            }

            TextInputField {
                objectName: "nextField"
                y: 60
                width: parent.width
            }
        }
    }

    function createHost(properties) {
        const host = createTemporaryObject(hostComponent, testCase, properties ?? {});
        verify(host);
        host.field.forceActiveFocus();
        return host;
    }

    function init() {
        failOnWarning(/.?/);
    }

    function test_nothing_opens_when_the_focus_arrives() {
        const host = createHost();
        compare(host.field.popupOpen, false);
    }

    function test_typing_opens_what_fits_ignoring_case() {
        const host = createHost();
        keyClick("l");
        compare(host.field.popupOpen, true);
        compare(host.field.matches, ["Liga okręgowa", "Puchar Polski", "Łódzka liga"]);
        keyClick("i");
        keyClick("g");
        compare(host.field.matches, ["Liga okręgowa", "Łódzka liga"]);
    }

    function test_polish_capitals_fit_too() {
        const host = createHost();
        // keyClick() takes ASCII only.
        host.field.text = "ł";
        compare(host.field.matches, ["Łódzka liga"]);
    }

    function test_text_that_fits_nothing_closes_them_and_stays() {
        const host = createHost();
        keyClick("l");
        compare(host.field.popupOpen, true);
        keyClick("x");
        compare(host.field.popupOpen, false);
        compare(host.field.text, "lx");
    }

    function test_a_suggestion_equal_to_the_text_is_not_offered() {
        const host = createHost({ "source": "Sparing" });
        compare(host.field.matches, []);
    }

    function test_a_suggestion_that_differs_in_case_stays_and_can_be_picked() {
        const host = createHost({ "source": "liga", "variants": ["Liga", "liga"] });
        compare(host.field.matches, ["Liga"]);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(host.field.text, "Liga");
    }

    function test_down_opens_every_suggestion_of_an_empty_field() {
        const host = createHost();
        keyClick(Qt.Key_Down);
        compare(host.field.popupOpen, true);
        compare(host.field.matches.length, 4);
    }

    function test_down_and_enter_pick_a_suggestion_without_going_on_to_the_form() {
        const host = createHost();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(host.field.text, "Puchar Polski");
        compare(host.field.popupOpen, false);
        compare(host.returnsSeen, 0);
    }

    function test_up_moves_back_and_stops_at_the_first() {
        const host = createHost();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Return);
        compare(host.field.text, "Liga okręgowa");
    }

    function test_a_picked_suggestion_keeps_the_binding_on_text() {
        const host = createHost();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(host.field.text, "Liga okręgowa");
        host.source = "Inna";
        compare(host.field.text, "Inna");
    }

    function test_enter_with_nothing_highlighted_goes_on_to_the_form() {
        const host = createHost();
        keyClick("l");
        compare(host.field.popupOpen, true);
        keyClick(Qt.Key_Return);
        compare(host.returnsSeen, 1);
        compare(host.field.text, "l");
    }

    function test_esc_closes_the_suggestions_first() {
        const host = createHost();
        keyClick("l");
        keyClick(Qt.Key_Escape);
        compare(host.field.popupOpen, false);
        compare(host.escapesSeen, 0);
        keyClick(Qt.Key_Escape);
        compare(host.escapesSeen, 1);
    }

    function test_leaving_the_field_closes_the_suggestions() {
        const host = createHost();
        keyClick("l");
        compare(host.field.popupOpen, true);
        findChild(host, "nextField").forceActiveFocus();
        compare(host.field.popupOpen, false);
    }

    function test_any_other_text_is_valid() {
        const host = createHost();
        for (const character of "Turniej")
            keyClick(character);
        compare(host.field.text, "Turniej");
        compare(host.field.effectiveError, "");
    }
}
