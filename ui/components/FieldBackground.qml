import QtQuick

// Background shared by the text-like inputs (TextInputField, NumberField, DateField, SelectField,
// SearchField): border, focus ring and error state. Not used on its own in views.
// API: active (input has focus), hasError, hovered.
Rectangle {
    id: root

    property bool active: false
    property bool hasError: false
    property bool hovered: false

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.controlHeight
    radius: Theme.radius.m
    color: Theme.surfaceAlt
    border.width: root.active || root.hasError ? Theme.focusRingWidth : Theme.borderWidth
    border.color: root.hasError ? Theme.danger : root.active ? Theme.accent : root.hovered ? Theme.textMuted : Theme.border
}

