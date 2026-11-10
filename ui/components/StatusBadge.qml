import QtQuick
import QtQuick.Controls.Basic

// Status is never color-only: color + icon + word.
Rectangle {
    id: root

    enum Kind {
        Ok,
        Warn,
        Danger,
        Info
    }

    property int kind: StatusBadge.Info
    property string text

    readonly property color tone: {
        switch (kind) {
        case StatusBadge.Ok:
            return Theme.ok;
        case StatusBadge.Warn:
            return Theme.warn;
        case StatusBadge.Danger:
            return Theme.danger;
        default:
            return Theme.info;
        }
    }
    readonly property string iconName: {
        switch (kind) {
        case StatusBadge.Ok:
            return "ok";
        case StatusBadge.Warn:
            return "warn";
        case StatusBadge.Danger:
            return "danger";
        default:
            return "info";
        }
    }

    implicitWidth: row.implicitWidth + 2 * Theme.spacing.s
    implicitHeight: Math.max(Theme.badgeHeight, row.implicitHeight + 2 * Theme.spacing.xs)
    radius: height / 2
    color: "transparent"
    border.width: Theme.borderWidth
    border.color: tone

    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.spacing.xs

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            size: Theme.iconSize.xs
            color: root.tone
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.tone
            font.pixelSize: Theme.fontSize.caption
            font.weight: Font.DemiBold
        }
    }
}
