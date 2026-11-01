import QtQuick
import QtQuick.Controls.Basic

// Search box with a search icon and a clear button. `text` follows the typed text after
// Theme.searchDebounce ms, so textChanged is debounced; clearing is immediate.
// API:
//   text: string (read-only, debounced), placeholderText
//   accepted()            Enter pressed (text is flushed first)
//   clear(), forceInputFocus()
// Esc clears a non-empty field; on an empty field it is passed on (e.g. to close a dialog).
FocusScope {
    id: root

    readonly property string text: internal.committedText
    property string placeholderText: qsTr("Search…")

    signal accepted

    function clear() {
        input.clear();
        debounce.stop();
        internal.committedText = "";
    }

    function forceInputFocus() {
        input.forceActiveFocus(Qt.ShortcutFocusReason);
        input.selectAll();
    }

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.controlHeight
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    QtObject {
        id: internal

        property string committedText
    }

    Timer {
        id: debounce

        interval: Theme.searchDebounce
        onTriggered: internal.committedText = input.text
    }

    TextField {
        id: input

        anchors.fill: parent
        focus: true
        leftPadding: Theme.spacing.s + Theme.iconSize.m + Theme.spacing.s
        rightPadding: clearButton.visible ? clearButton.width + Theme.spacing.xs : Theme.spacing.m
        placeholderText: root.placeholderText
        placeholderTextColor: Theme.textMuted
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        font.pixelSize: Theme.fontSize.body
        hoverEnabled: true
        Accessible.role: Accessible.EditableText
        Accessible.name: root.Accessible.name.length > 0 ? root.Accessible.name : qsTr("Search")
        onTextEdited: {
            if (text.length === 0)
                root.clear();
            else
                debounce.restart();
        }
        onAccepted: {
            debounce.stop();
            internal.committedText = text;
            root.accepted();
        }
        Keys.onEscapePressed: event => {
            if (text.length > 0)
                root.clear();
            else
                event.accepted = false;
        }

        background: FieldBackground {
            active: input.activeFocus
            hovered: input.hovered
        }

        Icon {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.s
            anchors.verticalCenter: parent.verticalCenter
            name: "search"
            size: Theme.iconSize.m
            color: Theme.textMuted
        }

        IconButton {
            id: clearButton

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: input.text.length > 0
            iconName: "close"
            iconColor: Theme.textMuted
            text: qsTr("Clear search")
            focusPolicy: Qt.NoFocus
            onClicked: {
                root.clear();
                input.forceActiveFocus();
            }
        }
    }
}

