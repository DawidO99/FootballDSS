pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// Drop-down list of checkboxes, one Tab stop in a form. Space or Down opens it; in the list Up/Down
// move, Space toggles, Enter and Esc close. Enter on the closed field is passed on (e.g. to save a
// dialog).
// API:
//   model: var              [{ value, text }]
//   values: var             input: array of the selected values; never assigned by the field
//   currentValues           read-only: `values`, or the user's last edit since `values` changed,
//                           in the order of the model
//   placeholderText         shown while nothing is selected
//   displayText             read-only: the selected texts
//   error: string           external message, empty = valid (red border)
//   effectiveError          read-only
//   popupOpen               read-only
//   valuesEdited(var values)   the user toggled an item
//   openPopup(), closePopup(), toggle(value)
// An edit stays inside the field, so a binding on `values` survives it. Keep a viewmodel in sync in
// valuesEdited; whenever `values` changes the field shows it again. Selected values that are not in
// the model are dropped from the next edit.
// Set Accessible.name to the field label.
FocusScope {
    id: root

    property var model: []
    property var values: []
    property string placeholderText
    property string error
    readonly property var currentValues: internal.selected
    readonly property string displayText: model.filter(option => internal.selected.includes(option.value)).map(option => option.text).join(", ")
    readonly property string effectiveError: error
    readonly property bool popupOpen: popup.visible

    signal valuesEdited(var values)

    function toggle(value) {
        const selected = internal.selected;
        const next = root.model.map(option => option.value).filter(candidate => candidate === value ? !selected.includes(candidate) : selected.includes(candidate));
        internal.selected = next;
        root.valuesEdited(next);
    }

    function openPopup() {
        if (!root.enabled || popup.visible)
            return;
        list.currentIndex = root.model.length > 0 ? 0 : -1;
        popup.open();
        list.forceActiveFocus(Qt.PopupFocusReason);
    }

    function closePopup() {
        popup.close();
    }

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.controlHeight
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    onValuesChanged: internal.selected = root.values ?? []
    Component.onCompleted: internal.selected = root.values ?? []

    QtObject {
        id: internal

        property var selected: []
    }

    AbstractButton {
        id: trigger

        anchors.fill: parent
        focus: true
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        leftPadding: Theme.spacing.m
        rightPadding: Theme.spacing.m + Theme.iconSize.m + Theme.spacing.s
        Accessible.role: Accessible.ComboBox
        Accessible.name: root.Accessible.name
        Accessible.description: root.displayText
        Keys.onDownPressed: root.openPopup()
        onClicked: popup.visible ? root.closePopup() : root.openPopup()

        background: FieldBackground {
            active: trigger.activeFocus || popup.visible
            hasError: root.effectiveError.length > 0
            hovered: trigger.hovered
        }

        contentItem: Label {
            text: root.displayText.length > 0 ? root.displayText : root.placeholderText
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSize.body
            color: root.displayText.length > 0 ? Theme.text : Theme.textMuted
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Icon {
            x: trigger.width - width - Theme.spacing.m
            y: (trigger.height - height) / 2
            name: popup.visible ? "chevronUp" : "chevronDown"
            size: Theme.iconSize.m
            color: Theme.textMuted
        }
    }

    Popup {
        id: popup

        y: root.height + Theme.spacing.xs
        width: root.width
        height: Math.min(list.contentHeight + 2 * padding, Theme.popupMaxHeight)
        margins: Theme.spacing.s
        padding: Theme.spacing.xs
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        onClosed: trigger.forceActiveFocus(Qt.PopupFocusReason)

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius.m
            border.width: Theme.borderWidth
            border.color: Theme.border
        }

        contentItem: ListView {
            id: list

            objectName: "multiSelectList"
            clip: true
            focus: true
            model: root.model
            boundsBehavior: Flickable.StopAtBounds
            Keys.onSpacePressed: {
                if (currentIndex >= 0)
                    root.toggle(root.model[currentIndex].value);
            }
            Keys.onReturnPressed: root.closePopup()
            Keys.onEnterPressed: root.closePopup()

            ScrollIndicator.vertical: ScrollIndicator {}

            delegate: CheckBoxField {
                required property var modelData
                required property int index

                width: ListView.view ? ListView.view.width : implicitWidth
                implicitHeight: Theme.rowHeight
                focusPolicy: Qt.NoFocus
                text: modelData.text
                checked: internal.selected.includes(modelData.value)
                highlighted: ListView.isCurrentItem
                onToggled: {
                    list.currentIndex = index;
                    root.toggle(modelData.value);
                }
            }
        }
    }
}
