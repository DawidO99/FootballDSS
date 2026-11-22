import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The direction of a custom metric against its previous value: arrow + word + color, never
// color-only. Green when the change goes the way "higher is better" says, red otherwise.
// API:
//   trend: string           "up", "down", "same" or "none" (nothing is shown)
//   improvement: bool       the change is for the better
//   text: string            the word, e.g. "Better" (from the viewmodel)
RowLayout {
    id: root

    property string trend: "none"
    property bool improvement: false
    property string text
    readonly property color tone: root.trend === "same" ? Theme.textMuted : root.improvement ? Theme.ok : Theme.danger
    readonly property string iconName: root.trend === "up" ? "arrow-up" : root.trend === "down" ? "arrow-down" : "minus"

    visible: root.trend !== "none"
    spacing: Theme.spacing.xs

    Icon {
        objectName: "trendIcon"
        name: root.iconName
        size: Theme.iconSize.s
        color: root.tone
    }

    Label {
        objectName: "trendText"
        text: root.text
        textFormat: Text.PlainText
        color: root.tone
        font.pixelSize: Theme.fontSize.caption
        font.weight: Font.DemiBold
    }
}
