import QtQuick
import QtQuick.Controls.Basic

// A tooltip for text that holds or may hold user data. The attached ToolTip.text guesses rich text,
// so markup in a name would become a link or an image (AGENTS section 6).
// API (inherited from ToolTip): text, visible, delay, timeout. Declare it inside the item it is for:
//   PlainToolTip {
//       visible: button.hovered
//       text: button.name
//       delay: Theme.tooltipDelay
//   }
ToolTip {
    id: control

    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        wrapMode: Text.Wrap
        color: control.palette.toolTipText
    }
}
