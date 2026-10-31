import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Title row at the top of every view; later packets add actions to the right.
RowLayout {
    id: root

    property string title

    spacing: Theme.spacing.m

    Label {
        Layout.fillWidth: true
        text: root.title
        color: Theme.text
        font.pixelSize: Theme.fontSize.h1
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
}
