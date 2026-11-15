pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

// The legend of a chart: Qt Graphs has no legend type (ADR-006). Every entry shows the shape of its
// series and a word, so the series are told apart without color too.
// API:
//   items: var    [{ label, color, shape, borderColor? }]
//                 shape: bar (filled), barOutline, line, dashedLine or band; borderColor: the edge
//                 of a band
Flow {
    id: root

    property var items: []

    spacing: Theme.spacing.l

    Repeater {
        model: root.items

        delegate: Row {
            id: entry

            required property var modelData

            spacing: Theme.spacing.xs

            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.chart.legendSwatchWidth
                height: Theme.iconSize.s

                Rectangle {
                    anchors.centerIn: parent
                    visible: entry.modelData.shape === "bar" || entry.modelData.shape === "barOutline"
                    width: Theme.chart.markerSize
                    height: parent.height
                    color: entry.modelData.shape === "bar" ? entry.modelData.color : "transparent"
                    border.width: Theme.chart.lineWidth
                    border.color: entry.modelData.color
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: entry.modelData.shape === "line"
                    width: parent.width
                    height: Theme.chart.lineWidth
                    color: entry.modelData.color
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: entry.modelData.shape === "dashedLine"
                    spacing: Theme.spacing.xs / 2

                    Repeater {
                        model: 3

                        Rectangle {
                            width: Theme.spacing.xs
                            height: Theme.chart.lineWidth
                            color: entry.modelData.color
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: entry.modelData.shape === "band"
                    radius: Theme.radius.xs
                    color: entry.modelData.color
                    border.width: Theme.borderWidth
                    border.color: entry.modelData.borderColor ?? entry.modelData.color
                }
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: entry.modelData.label
                textFormat: Text.PlainText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
            }
        }
    }
}
