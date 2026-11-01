pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// Drop-down selection.
// API:
//   model, textRole, valueRole, currentIndex, currentValue, currentText   (inherited from ComboBox)
//   value: var              selected value; the current item follows indexOfValue(value)
//   placeholderText         shown while nothing is selected
//   error: string           external message, empty = valid (red border)
//   valueActivated(var value)   the user picked an item (`value` is already updated)
// Set Accessible.name to the field label.
ComboBox {
    id: control

    property var value
    property string placeholderText
    property string error
    readonly property string effectiveError: error

    signal valueActivated(var value)

    function syncIndex() {
        control.currentIndex = control.value === undefined || control.value === null ? -1 : control.indexOfValue(control.value);
    }

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacing.m
    rightPadding: Theme.spacing.m + Theme.iconSize.m + Theme.spacing.s
    hoverEnabled: true
    font.pixelSize: Theme.fontSize.body
    displayText: currentIndex < 0 ? placeholderText : currentText
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    Accessible.name: placeholderText

    onValueChanged: syncIndex()
    onModelChanged: syncIndex()
    Component.onCompleted: syncIndex()
    onActivated: {
        control.value = control.currentValue;
        control.valueActivated(control.value);
    }

    background: FieldBackground {
        active: control.activeFocus || control.popup.visible
        hasError: control.effectiveError.length > 0
        hovered: control.hovered
    }

    contentItem: Label {
        text: control.displayText
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
        implicitHeight: Theme.rowHeight
        leftPadding: Theme.spacing.m
        text: control.textRole.length > 0 ? option.model[control.textRole] : option.model.modelData
        highlighted: control.highlightedIndex === index
        hoverEnabled: true

        background: Rectangle {
            color: option.highlighted ? Theme.selection : option.hovered ? Theme.rowHover : "transparent"
        }

        contentItem: Label {
            text: option.text
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

