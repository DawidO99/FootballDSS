pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// The steps of a wizard in one row (import, packet P16). A done step shows a tick, the current one
// its number in the accent colour and a bold title, a later one its number muted: never color-only.
// API:
//   steps: var           titles, in order
//   currentIndex: int    the current step
RowLayout {
    id: root

    property var steps: []
    property int currentIndex: 0

    spacing: Theme.spacing.m
    Accessible.role: Accessible.ProgressBar
    Accessible.name: steps.length > 0 && currentIndex >= 0 && currentIndex < steps.length ? qsTr("Step %1 of %2: %3").arg(currentIndex + 1).arg(steps.length).arg(steps[currentIndex]) : ""

    Repeater {
        model: root.steps

        delegate: RowLayout {
            id: stepItem

            required property int index
            required property string modelData
            readonly property bool done: index < root.currentIndex
            readonly property bool current: index === root.currentIndex

            spacing: Theme.spacing.s
            Layout.fillWidth: false

            Rectangle {
                Layout.preferredWidth: Theme.badgeHeight
                Layout.preferredHeight: Theme.badgeHeight
                radius: Theme.badgeHeight / 2
                color: stepItem.current ? Theme.accent : "transparent"
                border.width: Theme.borderWidth
                border.color: stepItem.current ? Theme.accent : stepItem.done ? Theme.ok : Theme.border

                Icon {
                    anchors.centerIn: parent
                    visible: stepItem.done
                    name: "check"
                    color: Theme.ok
                    size: Theme.iconSize.xs
                }

                Text {
                    anchors.centerIn: parent
                    visible: !stepItem.done
                    text: stepItem.index + 1
                    color: stepItem.current ? Theme.accentText : Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                    font.bold: stepItem.current
                }
            }

            Text {
                text: stepItem.modelData
                textFormat: Text.PlainText
                color: stepItem.current ? Theme.text : Theme.textMuted
                font.pixelSize: Theme.fontSize.body
                font.bold: stepItem.current
                elide: Text.ElideRight
                Layout.maximumWidth: Theme.importStepMaxWidth
            }

            Rectangle {
                visible: stepItem.index < root.steps.length - 1
                Layout.preferredWidth: Theme.spacing.xl
                Layout.preferredHeight: Theme.borderWidth
                color: Theme.border
            }
        }
    }
}
