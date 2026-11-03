pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// Free text with suggestions from earlier entries. Typing opens the suggestions that contain the
// text, ignoring case; any other text is just as valid. Down opens them or moves down, Up moves up,
// Enter takes the highlighted one and Esc closes them. With nothing highlighted Enter goes on to the
// form. The focus stays in the text.
// API:
//   text, placeholderText, error, effectiveError   (see TextInputField)
//   suggestions: var        [string]
//   matches                 read-only: the suggestions that fit the text, without the text itself
//   popupOpen               read-only
//   highlightedIndex        the suggestion Enter would take, in `matches`; -1 = none
// A picked suggestion changes `text` without breaking a binding on it; keep a viewmodel in sync in
// onTextChanged. Set Accessible.name to the field label.
TextInputField {
    id: control

    property var suggestions: []
    readonly property var matches: matchesOf(control.text)
    readonly property bool popupOpen: popup.visible
    // -1: nothing. Not the currentIndex of the list, which goes to 0 whenever its model changes, and
    // Enter would then take a suggestion nobody chose.
    property int highlightedIndex: -1

    function matchesOf(text) {
        const typed = text.trim().toLocaleLowerCase();
        return control.suggestions.filter(suggestion => {
            const candidate = suggestion.toLocaleLowerCase();
            return candidate !== typed && candidate.includes(typed);
        });
    }

    function pick(suggestion) {
        control.remove(0, control.length);
        control.insert(0, suggestion);
        popup.close();
    }

    function moveHighlight(step) {
        const count = control.matches.length;
        if (count === 0)
            return;
        control.highlightedIndex = Math.max(0, Math.min(count - 1, control.highlightedIndex + step));
        list.positionViewAtIndex(control.highlightedIndex, ListView.Contain);
    }

    onTextEdited: {
        control.highlightedIndex = -1;
        // textEdited comes before textChanged, so `matches` still has the last text here.
        if (control.matchesOf(control.text).length > 0)
            popup.open();
        else
            popup.close();
    }
    onActiveFocusChanged: {
        if (!control.activeFocus)
            popup.close();
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Down) {
            if (popup.visible) {
                control.moveHighlight(1);
            } else if (control.matches.length > 0) {
                control.highlightedIndex = -1;
                popup.open();
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Up && popup.visible) {
            control.moveHighlight(-1);
            event.accepted = true;
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && popup.visible && control.highlightedIndex >= 0) {
            control.pick(control.matches[control.highlightedIndex]);
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape && popup.visible) {
            popup.close();
            event.accepted = true;
        }
    }

    Popup {
        id: popup

        objectName: "suggestionsPopup"
        y: control.height + Theme.spacing.xs
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, Theme.popupMaxHeight)
        padding: Theme.spacing.xs
        focus: false
        closePolicy: Popup.CloseOnPressOutsideParent

        contentItem: ListView {
            id: list

            clip: true
            implicitHeight: contentHeight
            model: control.matches
            boundsBehavior: Flickable.StopAtBounds

            ScrollIndicator.vertical: ScrollIndicator {}

            delegate: ItemDelegate {
                id: option

                required property string modelData
                required property int index

                width: ListView.view ? ListView.view.width : implicitWidth
                implicitHeight: Theme.rowHeight
                leftPadding: Theme.spacing.m
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                text: modelData
                highlighted: index === control.highlightedIndex
                onClicked: control.pick(option.modelData)

                background: Rectangle {
                    color: option.highlighted ? Theme.selection : option.hovered ? Theme.rowHover : "transparent"
                }

                contentItem: Label {
                    text: option.text
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSize.body
                    color: Theme.text
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius.m
            border.width: Theme.borderWidth
            border.color: Theme.border
        }
    }
}
