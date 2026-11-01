pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Styled TabBar + StackLayout. Left/Right arrow keys switch tabs when the bar has focus.
// API:
//   titles: list<string>    tab labels in order
//   currentIndex: int       active tab index
//   pages (default)         one child Item per tab inside the StackLayout
FocusScope {
    id: root

    property list<string> titles
    property alias currentIndex: tabBar.currentIndex
    default property alias pages: stack.data

    implicitWidth: mainColumn.implicitWidth
    implicitHeight: mainColumn.implicitHeight

    ColumnLayout {
        id: mainColumn

        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabBar

            Layout.fillWidth: true
            focus: true
            activeFocusOnTab: true
            Accessible.role: Accessible.PageTabList
            Keys.onLeftPressed: {
                if (currentIndex > 0)
                    currentIndex -= 1;
            }
            Keys.onRightPressed: {
                if (currentIndex + 1 < count)
                    currentIndex += 1;
            }

            background: Rectangle {
                color: "transparent"
            }

            Repeater {
                // By count, not by list: retranslating qsTr() titles assigns a new list, and
                // rebuilding the buttons would send TabBar back to the first tab.
                model: root.titles.length

                delegate: TabButton {
                    id: tabButton

                    required property int index
                    readonly property bool selected: tabBar.currentIndex === index

                    implicitWidth: tabLabel.implicitWidth + 2 * Theme.spacing.l
                    implicitHeight: Theme.controlHeight + Theme.spacing.xs
                    text: root.titles[index] ?? ""
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    Accessible.role: Accessible.PageTab
                    Accessible.name: text

                    background: Rectangle {
                        color: tabButton.hovered && !tabButton.selected ? Theme.rowHover : "transparent"
                        radius: Theme.radius.s
                        border.width: tabButton.selected && tabBar.activeFocus ? Theme.focusRingWidth : 0
                        border.color: Theme.accent

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: Theme.focusRingWidth
                            visible: tabButton.selected
                            color: Theme.accent
                        }
                    }

                    contentItem: Label {
                        id: tabLabel

                        text: tabButton.text
                        color: tabButton.selected ? Theme.text : Theme.textMuted
                        font.pixelSize: Theme.fontSize.body
                        font.weight: tabButton.selected ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.borderWidth
            color: Theme.border
        }

        StackLayout {
            id: stack

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spacing.l
            currentIndex: tabBar.currentIndex
        }
    }
}
