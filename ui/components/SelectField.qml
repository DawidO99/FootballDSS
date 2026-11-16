pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// Drop-down selection.
// API:
//   model, textRole, valueRole, currentIndex, currentValue, currentText   (inherited from ComboBox)
//   value: var              input: selected value; never assigned by the field
//   placeholderText         shown while nothing is selected
//   error: string           external message, empty = valid (red border)
//   touch: bool             Theme.hitTouch high, the rows of the list too (tactics board, Match Day)
//   valueActivated(var value)   the user picked an item (read it from `currentValue` too)
//   showValue()             shows `value` again after a pick the owner did not accept
// A pick stays inside the field, so a binding on `value` survives it. Keep a viewmodel in sync in
// valueActivated; whenever `value` changes the field shows it again. The selection is looked up
// again when the model changes, so rows may arrive after the value.
// Set Accessible.name to the field label.
ComboBox {
    id: control

    property var value
    property string placeholderText
    property string error
    property bool touch: false
    readonly property string effectiveError: error

    signal valueActivated(var value)

    function syncIndex() {
        const selected = internal.selectedValue;
        control.currentIndex = selected === undefined || selected === null ? -1 : control.indexOfValue(selected);
    }

    // Shows `value` again, for a pick that the owner did not accept: `value` itself did not change.
    function showValue() {
        internal.selectedValue = control.value;
        syncIndex();
    }

    implicitWidth: Theme.fieldWidth
    implicitHeight: control.touch ? Theme.controlHeightTouch : Theme.controlHeight
    leftPadding: Theme.spacing.m
    rightPadding: Theme.spacing.m + Theme.iconSize.m + Theme.spacing.s
    hoverEnabled: true
    font.pixelSize: Theme.fontSize.body
    displayText: currentIndex < 0 ? placeholderText : currentText
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.name: placeholderText

    onValueChanged: {
        internal.selectedValue = control.value;
        syncIndex();
    }
    onModelChanged: syncIndex()
    onCountChanged: syncIndex()
    Component.onCompleted: {
        internal.selectedValue = control.value;
        syncIndex();
    }
    onActivated: {
        internal.selectedValue = control.currentValue;
        control.valueActivated(control.currentValue);
    }

    QtObject {
        id: internal

        property var selectedValue
        // Duck typing: `model` may also be a JS array, which is not a Connections target.
        readonly property var itemModel: control.model && control.model.modelReset !== undefined ? control.model : null
    }

    // A reset with the same row count does not change `count`. The later call lets ComboBox's own
    // model handling finish first, whatever the connection order.
    Connections {
        target: internal.itemModel

        function onRowsInserted() {
            Qt.callLater(control.syncIndex);
        }

        function onModelReset() {
            Qt.callLater(control.syncIndex);
        }
    }

    background: FieldBackground {
        active: control.activeFocus || control.popup.visible
        hasError: control.effectiveError.length > 0
        hovered: control.hovered
    }

    contentItem: Label {
        text: control.displayText
        textFormat: Text.PlainText
        font: control.font
        color: control.currentIndex < 0 ? Theme.textMuted : Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Icon {
        x: control.width - width - Theme.spacing.m
        y: (control.height - height) / 2
        name: control.popup.visible ? "chevronUp" : "chevronDown"
        size: Theme.iconSize.m
        color: Theme.textMuted
    }

    delegate: ItemDelegate {
        id: option

        required property var model
        required property int index

        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: control.touch ? Theme.hitTouch : Theme.rowHeight
        leftPadding: Theme.spacing.m
        text: control.textRole.length > 0 ? option.model[control.textRole] : option.model.modelData
        highlighted: control.highlightedIndex === index
        hoverEnabled: true

        background: Rectangle {
            color: option.highlighted ? Theme.selection : option.hovered ? Theme.rowHover : "transparent"
        }

        contentItem: Label {
            text: option.text
            textFormat: Text.PlainText
            font.pixelSize: Theme.fontSize.body
            font.weight: control.currentIndex === option.index ? Font.DemiBold : Font.Normal
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    popup: Popup {
        y: control.height + Theme.spacing.xs
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, Theme.popupMaxHeight)
        padding: Theme.spacing.xs

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds

            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius.m
            border.width: Theme.borderWidth
            border.color: Theme.border
        }
    }
}

