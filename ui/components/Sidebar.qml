pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import FootballDSS

// Collapsible main navigation. Up/Down switch sections while the list has keyboard focus.
Rectangle {
    id: root

    required property NavigationModel navigation
    property bool collapsed: false

    readonly property int expandedWidth: 220
    readonly property int collapsedWidth: 64

    implicitWidth: collapsed ? collapsedWidth : expandedWidth
    color: Theme.surface
    clip: true

    Behavior on implicitWidth {
        NumberAnimation {
            duration: 120
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s
        spacing: Theme.spacing.s

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            objectName: "sidebarList"
            model: root.navigation
            currentIndex: root.navigation.currentIndex
            onCurrentIndexChanged: root.navigation.currentIndex = currentIndex
            spacing: Theme.spacing.xs
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            activeFocusOnTab: true

            Accessible.role: Accessible.PageTabList
            Accessible.name: qsTr("Main navigation")

            delegate: SidebarButton {
                required property int index
                required property string title
                required iconName

                width: list.width
                text: title
                collapsed: root.collapsed
                current: ListView.isCurrentItem
                showFocusRing: ListView.isCurrentItem && list.activeFocus
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.PageTab
                onClicked: root.navigation.currentIndex = index
            }
        }

        SidebarButton {
            Layout.fillWidth: true
            iconName: root.collapsed ? "chevronRight" : "chevronLeft"
            text: root.collapsed ? qsTr("Expand sidebar") : qsTr("Collapse sidebar")
            collapsed: root.collapsed
            focusPolicy: Qt.TabFocus
            onClicked: root.collapsed = !root.collapsed
        }
    }
}
