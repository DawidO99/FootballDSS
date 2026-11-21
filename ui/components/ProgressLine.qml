import QtQuick
import QtQuick.Layouts

// A thin progress bar with a text below it, for work on the thread pool (reading, validation).
// API:
//   value: real    0-1; below 0 = the length of the work is unknown (a sweeping segment)
//   text: string   what is being done, e.g. "Checking the rows… 40%"
ColumnLayout {
    id: root

    property real value: -1
    property string text
    readonly property bool indeterminate: value < 0

    spacing: Theme.spacing.xs
    Accessible.role: Accessible.ProgressBar
    Accessible.name: text

    Rectangle {
        id: track

        Layout.fillWidth: true
        Layout.preferredHeight: Theme.progressHeight
        radius: Theme.progressHeight / 2
        color: Theme.surfaceAlt
        clip: true

        Rectangle {
            id: fill

            height: parent.height
            radius: parent.radius
            color: Theme.accent
            width: root.indeterminate ? track.width / 4 : track.width * Math.max(0, Math.min(1, root.value))

            NumberAnimation on x {
                running: root.visible && root.indeterminate
                from: -fill.width
                to: track.width
                duration: Theme.progressSweep
                loops: Animation.Infinite
            }

            Binding on x {
                when: !root.indeterminate
                value: 0
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.text.length > 0
        text: root.text
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.caption
        wrapMode: Text.Wrap
    }
}
