pragma ComponentBehavior: Bound

import QtQuick

// On-screen keys for a PIN field, with touch-sized hit targets. The keys never take the focus, so
// typing and Enter keep working in the field next to them; they are not reachable by Tab because the
// field does everything they do.
// API:
//   digitPressed(string digit), backspacePressed(), clearPressed()
Grid {
    id: root

    signal digitPressed(string digit)
    signal backspacePressed
    signal clearPressed

    component DigitKey: SecondaryButton {
        width: Theme.pinKeyWidth
        touch: true
        focusPolicy: Qt.NoFocus
        onClicked: root.digitPressed(text)
    }

    columns: 3
    spacing: Theme.spacing.s
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Repeater {
        model: ["1", "2", "3", "4", "5", "6", "7", "8", "9"]

        delegate: DigitKey {
            required property string modelData

            text: modelData
        }
    }

    IconButton {
        objectName: "clearKey"
        width: Theme.pinKeyWidth
        touch: true
        focusPolicy: Qt.NoFocus
        iconName: "close"
        text: qsTr("Clear (Esc)")
        onClicked: root.clearPressed()
    }

    DigitKey {
        text: "0"
    }

    IconButton {
        objectName: "backspaceKey"
        width: Theme.pinKeyWidth
        touch: true
        focusPolicy: Qt.NoFocus
        iconName: "chevronLeft"
        text: qsTr("Delete the last digit")
        onClicked: root.backspacePressed()
    }
}
