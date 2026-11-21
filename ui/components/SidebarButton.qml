import QtQuick
import QtQuick.Controls.Basic

// Sidebar entry: icon + label, icon only (with tooltip) when the sidebar is collapsed. A locked
// section (P15) keeps its entry with a lock and the word in the tooltip, never color alone.
AbstractButton {
    id: control

    property string iconName
    property bool collapsed: false
    property bool current: false
    property bool showFocusRing: visualFocus
    property bool locked: false
    // What is missing; shown in the tooltip of a locked entry.
    property string lockHint

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Math.max(Theme.hitMin, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Theme.spacing.m
    rightPadding: Theme.spacing.m
    topPadding: Theme.spacing.s
    bottomPadding: Theme.spacing.s
    hoverEnabled: true

    Accessible.name: locked ? qsTr("%1, locked").arg(text) : text
    Accessible.description: locked ? lockHint : ""
    ToolTip.visible: (collapsed || locked) && hovered
    ToolTip.text: locked ? qsTr("%1 (locked): %2").arg(text).arg(lockHint) : text
    ToolTip.delay: Theme.tooltipDelay

    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: Theme.radius.m
        color: control.current || control.hovered ? Theme.surfaceAlt : "transparent"
        border.width: control.showFocusRing ? Theme.focusRingWidth : 0
        border.color: Theme.accent

        Rectangle {
            visible: control.current
            width: Theme.sidebarMarkerWidth
            radius: Theme.radius.xs
            color: Theme.accent
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacing.s
            anchors.bottomMargin: Theme.spacing.s
        }
    }

    contentItem: Row {
        spacing: Theme.spacing.m

        Item {
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: sectionIcon.implicitWidth
            implicitHeight: sectionIcon.implicitHeight

            Icon {
                id: sectionIcon

                name: control.iconName
                color: control.current ? Theme.accent : Theme.textMuted
            }

            // Collapsed, the lock sits on the corner of the section icon.
            Icon {
                objectName: "collapsedLockIcon"
                visible: control.locked && control.collapsed
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: -Theme.spacing.xs
                anchors.bottomMargin: -Theme.spacing.xs
                name: "lock"
                size: Theme.iconSize.xs
                color: Theme.warn
            }
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            visible: !control.collapsed
            text: control.text
            color: control.current ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.fontSize.body
            font.weight: control.current ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        Icon {
            objectName: "lockIcon"
            anchors.verticalCenter: parent.verticalCenter
            visible: control.locked && !control.collapsed
            name: "lock"
            size: Theme.iconSize.xs
            color: Theme.warn
        }
    }
}
