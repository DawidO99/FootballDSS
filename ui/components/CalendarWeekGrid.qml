pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One week of the calendar: seven equal columns, Monday first. Each item is a button: Tab reaches it,
// Enter, Space or a click opens it. A column scrolls when its items do not fit.
// API:
//   model                    CalendarWeekModel, or rows with { date, dayName, dateText, today, items }
//   itemActivated(string kind, double itemId)
RowLayout {
    id: root

    property var model

    signal itemActivated(string kind, double itemId)

    spacing: Theme.spacing.s

    Repeater {
        model: root.model

        delegate: Rectangle {
            id: dayColumn

            required property var model
            // A row of a JavaScript array comes as modelData, a row of a model under its role names.
            readonly property var record: model.modelData !== undefined ? model.modelData : model
            readonly property bool today: record.today === true

            objectName: "weekDay_" + record.date
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Theme.surface
            radius: Theme.radius.m
            border.width: dayColumn.today ? Theme.focusRingWidth : Theme.borderWidth
            border.color: dayColumn.today ? Theme.accent : Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing.s
                spacing: Theme.spacing.s

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: dayColumn.record.dayName
                        color: dayColumn.today ? Theme.accent : Theme.text
                        font.pixelSize: Theme.fontSize.body
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    // The word too: the border of today is not enough on its own (AGENTS section 6).
                    Label {
                        Layout.fillWidth: true
                        text: dayColumn.today ? qsTr("%1 · Today").arg(dayColumn.record.dateText) : dayColumn.record.dateText
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                        elide: Text.ElideRight
                    }
                }

                FocusFlickable {
                    id: dayItems

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: itemsColumn.implicitHeight

                    ColumnLayout {
                        id: itemsColumn

                        width: dayItems.width
                        spacing: Theme.spacing.xs

                        Repeater {
                            model: dayColumn.record.items

                            delegate: AbstractButton {
                                id: itemButton

                                required property var modelData

                                objectName: "weekItem_" + modelData.kind + "_" + modelData.itemId
                                Layout.fillWidth: true
                                implicitHeight: Math.max(Theme.hitMin, itemContent.implicitHeight + 2 * Theme.spacing.xs)
                                hoverEnabled: true
                                Accessible.role: Accessible.Button
                                Accessible.name: [modelData.kindText, modelData.time, modelData.title, modelData.details, modelData.statusText].filter(part => part.length > 0).join(", ")
                                Keys.onReturnPressed: click()
                                Keys.onEnterPressed: click()
                                onClicked: root.itemActivated(modelData.kind, modelData.itemId)

                                PlainToolTip {
                                    objectName: "itemToolTip"
                                    visible: itemButton.hovered
                                    text: itemButton.Accessible.name
                                    delay: Theme.tooltipDelay
                                }

                                background: Rectangle {
                                    radius: Theme.radius.s
                                    color: itemButton.down ? Theme.border : itemButton.hovered ? Theme.rowHover : Theme.surfaceAlt
                                    border.width: itemButton.activeFocus ? Theme.focusRingWidth : 0
                                    border.color: Theme.accent
                                }

                                contentItem: Item {
                                    implicitHeight: itemContent.implicitHeight

                                    ColumnLayout {
                                        id: itemContent

                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: Theme.spacing.xs
                                        anchors.rightMargin: Theme.spacing.xs
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 0

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.spacing.xs

                                            Icon {
                                                name: itemButton.modelData.kind === "match" ? "matchday" : "training"
                                                size: Theme.iconSize.s
                                                color: Theme.textMuted
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: itemButton.modelData.time.length > 0 ? qsTr("%1 · %2").arg(itemButton.modelData.time).arg(itemButton.modelData.kindText) : itemButton.modelData.kindText
                                                color: Theme.textMuted
                                                font.pixelSize: Theme.fontSize.caption
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: itemButton.modelData.title
                                            textFormat: Text.PlainText
                                            color: Theme.text
                                            font.pixelSize: Theme.fontSize.body
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: itemButton.modelData.details
                                            textFormat: Text.PlainText
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontSize.caption
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            visible: text.length > 0
                                            text: itemButton.modelData.statusText
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontSize.caption
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
