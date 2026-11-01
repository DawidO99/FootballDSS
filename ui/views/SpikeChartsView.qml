pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtGraphs

// P03 spike: does Qt Graphs cover our charts? Not in the sidebar; Ctrl+Shift+G toggles it.
// Sample data lives here on purpose; real charts get their data from a ViewModel.
Item {
    id: root

    signal navigateRequested(string key, var args)

    // 14 days of team load (sRPE, AU). Calendar days are plotted as UTC midnight because
    // DateTimeAxis labels are in UTC by default.
    readonly property list<real> dailyLoad: [420, 0, 510, 380, 0, 650, 300, 450, 0, 560, 410, 0, 700, 320]
    readonly property double firstDay: Date.UTC(2026, 9, 1)
    readonly property double dayMs: 24 * 60 * 60 * 1000

    component ChartTheme: GraphsTheme {
        theme: GraphsTheme.Theme.UserDefined
        colorScheme: Theme.dark ? GraphsTheme.ColorScheme.Dark : GraphsTheme.ColorScheme.Light
        seriesColors: Theme.chart.series
        borderColors: Theme.chart.series
        backgroundVisible: false
        plotAreaBackgroundVisible: false
        labelBackgroundVisible: false
        labelTextColor: Theme.textMuted
        labelFont.pixelSize: Theme.fontSize.caption
        axisXLabelFont.pixelSize: Theme.fontSize.caption
        axisYLabelFont.pixelSize: Theme.fontSize.caption
        grid.mainColor: Theme.border
        grid.subColor: Theme.surfaceAlt
        axisX.mainColor: Theme.border
        axisX.labelTextColor: Theme.textMuted
        axisY.mainColor: Theme.border
        axisY.labelTextColor: Theme.textMuted
    }

    component ChartCard: Rectangle {
        property alias title: titleLabel.text
        default property alias content: body.data

        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radius.m

        Label {
            id: titleLabel

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing.l
            color: Theme.text
            font.pixelSize: Theme.fontSize.title
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Item {
            id: body

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleLabel.bottom
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing.l
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Charts spike (Qt Graphs)")
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Hidden test view with sample data. Press Ctrl+Shift+G to close it.")
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        ChartCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Team load, last 14 days (AU)")

            GraphsView {
                id: loadChart

                anchors.fill: parent
                theme: ChartTheme {}
                // Points sit on the plot edges (first/last day, zero load); do not cut markers.
                clipPlotArea: false

                // Ticks split min..max evenly (no anchoring to midnight), so the range must start
                // and end on a data day and tickCount must give one tick per day.
                axisX: DateTimeAxis {
                    min: new Date(root.firstDay)
                    max: new Date(root.firstDay + (root.dailyLoad.length - 1) * root.dayMs)
                    tickCount: root.dailyLoad.length - 1
                    //: Date format of the chart axis (Qt date format: d = day, M = month).
                    labelFormat: qsTr("MM/dd")
                    gridVisible: false
                    subGridVisible: false
                }
                axisY: ValueAxis {
                    min: 0
                    max: 800
                    tickInterval: 200
                    labelDecimals: 0
                    subGridVisible: false
                }

                LineSeries {
                    id: loadSeries

                    name: qsTr("Team load")
                    width: Theme.chart.lineWidth
                    hoverable: true
                    pointDelegate: Rectangle {
                        width: Theme.chart.markerSize
                        height: Theme.chart.markerSize
                        radius: Theme.chart.markerSize / 2
                        color: Theme.chart.series1
                    }
                }

                // hover reports the interpolated point under the cursor, not a data point:
                // snap to the nearest day.
                onHover: (seriesName, position, value) => {
                    const day = Math.round((value.x - root.firstDay) / root.dayMs);
                    loadTip.x = position.x + Theme.spacing.m;
                    loadTip.y = position.y - loadTip.height - Theme.spacing.m;
                    loadTip.text = qsTr("%1: %2 AU").arg(Qt.formatDate(new Date(root.firstDay + day * root.dayMs))).arg(root.dailyLoad[day]);
                    loadTip.visible = true;
                }
                onHoverExit: loadTip.visible = false

                ToolTip {
                    id: loadTip
                }
            }
        }

        ChartCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Average session load by position (AU)")

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacing.s

                // Qt Graphs has no legend type: build it from the series' legendData.
                Row {
                    spacing: Theme.spacing.l

                    Repeater {
                        model: positionSeries.legendData

                        delegate: Row {
                            id: legendItem

                            required property var modelData

                            spacing: Theme.spacing.xs

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.chart.markerSize
                                height: Theme.chart.markerSize
                                radius: Theme.radius.s / 2
                                color: legendItem.modelData.color
                            }

                            Label {
                                text: legendItem.modelData.label
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                            }
                        }
                    }
                }

                GraphsView {
                    id: positionChart

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    theme: ChartTheme {}

                    axisX: BarCategoryAxis {
                        categories: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri")]
                        gridVisible: false
                    }
                    axisY: ValueAxis {
                        min: 0
                        max: 500
                        tickInterval: 100
                        labelDecimals: 0
                        subGridVisible: false
                    }

                    BarSeries {
                        id: positionSeries

                        hoverable: true

                        BarSet {
                            label: qsTr("Defenders")
                            values: [380, 420, 210, 450, 150]
                        }
                        BarSet {
                            label: qsTr("Midfielders")
                            values: [410, 460, 240, 480, 170]
                        }
                        BarSet {
                            label: qsTr("Forwards")
                            values: [350, 390, 200, 430, 140]
                        }
                    }

                    // hover reports the series name and (category index, value), not the bar
                    // set: find the set by its value in that category.
                    onHover: (seriesName, position, value) => {
                        let label = seriesName;
                        for (const barSet of positionSeries.barSets) {
                            if (barSet.values[value.x] === value.y) {
                                label = barSet.label;
                                break;
                            }
                        }
                        positionTip.x = position.x + Theme.spacing.m;
                        positionTip.y = position.y - positionTip.height - Theme.spacing.m;
                        positionTip.text = qsTr("%1: %2 AU").arg(label).arg(Math.round(value.y));
                        positionTip.visible = true;
                    }
                    onHoverExit: positionTip.visible = false

                    ToolTip {
                        id: positionTip
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        for (let day = 0; day < root.dailyLoad.length; ++day) {
            loadSeries.append(root.firstDay + day * root.dayMs, root.dailyLoad[day]);
        }
    }
}
