pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The issues of the lineup validator (SPEC section 7.2, P12): errors first, each with a badge
// (color + icon + word) and the text that names the player and the rule. A click, a tap or Enter on
// an issue selects its place on the board. The texts come from the viewmodel and may hold player
// names, so they are plain text.
// API:
//   model: LineupIssuesModel     severity, severityText, ruleKey, text, playerId, slotIndex, benchIndex
//   summary: string              e.g. "Errors: 2 · Warnings: 1"
//   errorCount: int
//   loadPending: bool            the load statuses are still being calculated
//   incompleteText: string       the check is incomplete (the load could not be read); empty: it is
//                                complete. Shown with a warning icon and a Try again button.
//   issueActivated(row)
//   retryRequested()
Item {
    id: root

    property var model
    property string summary
    property int errorCount: 0
    property bool loadPending: false
    property string incompleteText
    readonly property alias listView: list

    signal issueActivated(int row)
    signal retryRequested

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.issuesPanelHeight

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.m
        color: Theme.surface
        border.width: list.activeFocus ? Theme.focusRingWidth : Theme.borderWidth
        border.color: list.activeFocus ? Theme.accent : Theme.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing.m
            spacing: Theme.spacing.s

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.s

                Icon {
                    name: root.errorCount > 0 ? "danger" : list.count > 0 || root.incompleteText.length > 0 ? "warn" : "ok"
                    color: root.errorCount > 0 ? Theme.danger : list.count > 0 || root.incompleteText.length > 0 ? Theme.warn : Theme.ok
                    size: Theme.iconSize.m
                }

                Label {
                    objectName: "issuesSummary"
                    Layout.fillWidth: true
                    text: root.summary
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    visible: root.loadPending
                    text: qsTr("Checking the load…")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                }
            }

            RowLayout {
                objectName: "incompleteCheck"
                Layout.fillWidth: true
                visible: root.incompleteText.length > 0
                spacing: Theme.spacing.s

                Icon {
                    name: "warn"
                    color: Theme.warn
                    size: Theme.iconSize.s
                }

                Label {
                    Layout.fillWidth: true
                    text: root.incompleteText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.WordWrap
                }

                SecondaryButton {
                    objectName: "retryLoadButton"
                    text: qsTr("Try again")
                    onClicked: root.retryRequested()
                }
            }

            ListView {
                id: list

                objectName: "issuesListView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.model
                spacing: Theme.spacing.xs
                activeFocusOnTab: count > 0
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Lineup issues")
                Keys.onReturnPressed: root.issueActivated(list.currentIndex)
                Keys.onEnterPressed: root.issueActivated(list.currentIndex)

                delegate: Rectangle {
                    id: issue

                    required property int index
                    required property string severity
                    required property string severityText
                    required property string ruleKey
                    required property string text

                    objectName: "issue_" + issue.ruleKey
                    width: ListView.view.width
                    height: Math.max(Theme.hitMin, content.implicitHeight + 2 * Theme.spacing.xs)
                    radius: Theme.radius.s
                    color: ListView.isCurrentItem && list.activeFocus ? Theme.selection : issueHover.hovered ? Theme.rowHover : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: issue.severityText + ": " + issue.text

                    RowLayout {
                        id: content

                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing.xs
                        anchors.rightMargin: Theme.spacing.xs
                        spacing: Theme.spacing.s

                        StatusBadge {
                            Layout.alignment: Qt.AlignTop
                            kind: issue.severity === "error" ? StatusBadge.Danger : StatusBadge.Warn
                            text: issue.severityText
                        }

                        Label {
                            Layout.fillWidth: true
                            text: issue.text
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: Theme.text
                            font.pixelSize: Theme.fontSize.body
                        }
                    }

                    HoverHandler {
                        id: issueHover

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: {
                            list.currentIndex = issue.index;
                            root.issueActivated(issue.index);
                        }
                    }
                }
            }
        }
    }
}
