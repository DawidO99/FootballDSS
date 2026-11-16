import QtQuick
import QtQuick.Controls.Basic

// Status is never color-only: color + icon + word.
Rectangle {
    id: root

    enum Kind {
        Ok,
        Warn,
        Danger,
        Info,
        // Grey: no status yet (e.g. not enough data).
        Neutral
    }

    property int kind: StatusBadge.Info
    property string text
    // The font of the word, which implicitWidthFor() measures in.
    readonly property font wordFont: word.font

    readonly property color tone: {
        switch (kind) {
        case StatusBadge.Ok:
            return Theme.ok;
        case StatusBadge.Warn:
            return Theme.warn;
        case StatusBadge.Danger:
            return Theme.danger;
        case StatusBadge.Neutral:
            return Theme.textMuted;
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
        case StatusBadge.Neutral:
            return "minus";
        default:
            return "info";
        }
    }

    // The width of this badge with another text, in the font of its word and laid out as below: a
    // column fits its longest text before any row shows it.
    function implicitWidthFor(otherText) {
        return Theme.iconSize.xs + row.spacing + Math.ceil(wordMetrics.advanceWidth(otherText)) + 2 * Theme.spacing.s;
    }

    implicitWidth: row.implicitWidth + 2 * Theme.spacing.s
    implicitHeight: Math.max(Theme.badgeHeight, row.implicitHeight + 2 * Theme.spacing.xs)
    radius: height / 2
    color: "transparent"
    border.width: Theme.borderWidth
    border.color: tone

    Accessible.role: Accessible.StaticText
    Accessible.name: text

    FontMetrics {
        id: wordMetrics

        font: word.font
    }

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
            id: word

            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.tone
            font.pixelSize: Theme.fontSize.caption
            font.weight: Font.DemiBold
        }
    }
}
