pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The tooltip of a chart: Qt Graphs has none (ADR-006). The chart decides which data point is meant
// by its index, never by its value (SPEC section 4: two series may hold the same value), and fills
// the title and the rows.
// API:
//   title: string
//   rows: var       [{ label, value, color, shape }], shape as in ChartLegend; other keys (such as
//                   the series key) are kept
//   showAt(x, y)    next to that point of the parent, kept inside the parent
//   hide()
Rectangle {
    id: root

    property string title
    property var rows: []

    function showAt(pointX, pointY) {
        const gap = Theme.spacing.m;
        const right = pointX + gap + width <= parent.width;
        root.x = right ? pointX + gap : Math.max(0, pointX - gap - width);
        root.y = Math.max(0, Math.min(pointY - height / 2, parent.height - height));
        root.visible = true;
    }

    function hide() {
        root.visible = false;
    }

    visible: false
    z: 1
    width: Theme.chart.tooltipWidth
    height: column.implicitHeight + 2 * Theme.spacing.s
    color: Theme.surface
    radius: Theme.radius.s
    border.width: Theme.borderWidth
    border.color: Theme.border

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.margins: Theme.spacing.s
        spacing: Theme.spacing.xs

        Label {
            Layout.fillWidth: true
            text: root.title
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSize.caption
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Repeater {
            model: root.rows

            delegate: RowLayout {
                id: row

                required property var modelData

                Layout.fillWidth: true
                spacing: Theme.spacing.s

                ChartLegend {
                    Layout.fillWidth: true
                    items: [row.modelData]
                }

                Label {
                    text: row.modelData.value ?? ""
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.caption
                }
            }
        }
    }
}
