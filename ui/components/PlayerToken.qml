import QtQuick
import QtQuick.Controls.Basic

// A player on the tactics board (P12): the shirt number in a circle of Theme.hitTouch and the
// surname under it. An issue of the lineup validator adds an icon (a different one for an error and
// a warning; the words are in the tooltip and the accessible name), so it is never color only. A
// free slot shows its position, and can be dragged too: a custom shape can be built before any
// player is in it (P12 manual test).
// API:
//   positionText: string        GK, RB…
//   shirtNumber: var            a number, or nothing
//   shortName: string           the surname; empty: a free slot
//   fullName: string
//   severity: string            error, warning or empty
//   issueText: string           the issues, one per line
//   selected: bool
//   interactive: bool           can be dragged, also when free (the board is not read-only)
//   ghost: bool                 the copy that follows the pointer during a drag: no input
//   payload: var                what a drop target reads from a ghost: { kind, slot, playerId }
//   dragActive: bool            read-only: this token is being dragged
//   dragScenePosition: point    read-only: the pointer during the drag, in scene coordinates
//   activated()                 Enter or Space
//   tapped()                    a click or tap; the first of a double one too
//   doubleTapped()              the second click or tap of a double one
//   deletePressed()             Delete while it has the focus
//   benchPressed()              B while it has the focus (the accelerator of "Move to the bench")
//   dragReleased()              the drag ended
Item {
    id: root

    property string positionText
    property var shirtNumber
    property string shortName
    property string fullName
    property string severity
    property string issueText
    property bool selected: false
    property bool interactive: true
    property bool ghost: false
    property var payload: ({})
    readonly property bool empty: shortName.length === 0
    readonly property bool dragActive: dragHandler.active
    readonly property point dragScenePosition: dragHandler.centroid.scenePosition
    readonly property bool hasNumber: shirtNumber !== undefined && shirtNumber !== null
    readonly property string description: {
        const parts = [root.positionText];
        if (!root.empty)
            parts.push(root.hasNumber ? qsTr("%1, number %2").arg(root.fullName).arg(root.shirtNumber) : root.fullName);
        else
            parts.push(qsTr("free slot"));
        if (root.issueText.length > 0)
            parts.push(root.issueText);
        return parts.join("\n");
    }

    signal activated
    signal tapped
    signal doubleTapped
    signal deletePressed
    signal benchPressed
    signal dragReleased

    implicitWidth: Theme.pitch.labelWidth
    implicitHeight: Theme.pitch.tokenSize + Theme.pitch.labelHeight
    activeFocusOnTab: !ghost
    opacity: dragActive ? Theme.disabledOpacity : 1.0

    Accessible.role: Accessible.Button
    Accessible.name: root.description.replace(/\n/g, ". ")
    Keys.onReturnPressed: root.activated()
    Keys.onEnterPressed: root.activated()
    Keys.onSpacePressed: root.activated()
    Keys.onDeletePressed: root.deletePressed()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_B && event.modifiers === Qt.NoModifier) {
            root.benchPressed();
            event.accepted = true;
        }
    }

    Rectangle {
        id: circle

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        width: Theme.pitch.tokenSize
        height: Theme.pitch.tokenSize
        radius: width / 2
        color: root.empty ? "transparent" : Theme.pitch.token
        border.width: root.selected || root.activeFocus ? 2 * Theme.focusRingWidth : Theme.pitch.lineWidth
        border.color: root.selected || root.activeFocus ? Theme.accent : root.empty ? Theme.pitch.emptySlot : Theme.pitch.token

        Label {
            anchors.centerIn: parent
            text: root.empty || !root.hasNumber ? root.positionText : String(root.shirtNumber)
            textFormat: Text.PlainText
            color: root.empty ? Theme.pitch.line : Theme.pitch.tokenText
            font.pixelSize: root.empty ? Theme.fontSize.caption : Theme.fontSize.title
            font.weight: Font.DemiBold
        }
    }

    // The issue mark: on a disc of the token color, so it reads on the grass and on the token.
    Rectangle {
        objectName: "issueMark"
        visible: root.severity.length > 0
        anchors.right: circle.right
        anchors.top: circle.top
        anchors.rightMargin: -Theme.spacing.xs
        anchors.topMargin: -Theme.spacing.xs
        width: Theme.pitch.issueIconSize + Theme.spacing.xs
        height: width
        radius: width / 2
        color: Theme.pitch.token

        Icon {
            anchors.centerIn: parent
            name: root.severity === "error" ? "danger" : "warn"
            color: root.severity === "error" ? Theme.danger : Theme.warn
            size: Theme.pitch.issueIconSize
        }
    }

    Rectangle {
        visible: !root.empty
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: circle.bottom
        width: Math.min(nameLabel.implicitWidth + 2 * Theme.spacing.xs, root.width)
        height: Theme.pitch.labelHeight
        radius: Theme.radius.s
        color: Theme.pitch.labelBackground

        Label {
            id: nameLabel

            anchors.fill: parent
            anchors.leftMargin: Theme.spacing.xs
            anchors.rightMargin: Theme.spacing.xs
            text: root.shortName
            textFormat: Text.PlainText
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.pitch.labelText
            font.pixelSize: Theme.fontSize.caption
        }
    }

    TapHandler {
        enabled: !root.ghost
        onSingleTapped: {
            root.forceActiveFocus(Qt.MouseFocusReason);
            root.tapped();
        }
        onDoubleTapped: root.doubleTapped()
    }

    DragHandler {
        id: dragHandler

        target: null
        enabled: root.interactive && !root.ghost
        dragThreshold: Theme.pitch.dragThreshold
        onActiveChanged: {
            if (!active)
                root.dragReleased();
        }
    }

    HoverHandler {
        id: hover

        enabled: !root.ghost
    }

    PlainToolTip {
        visible: hover.hovered && !root.dragActive && !root.ghost
        text: root.description
        delay: Theme.tooltipDelay
    }
}
