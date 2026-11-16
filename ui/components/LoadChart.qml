pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtGraphs

// The load of one player over the last weeks (packet P11, SPEC section 7.1), in Qt Graphs.
// Top: the daily load as stacked bars, sessions filled and matches outlined (told apart without
// color too), with the acute (solid) and chronic (dashed) EWMA lines. Bottom: the acute:chronic
// ratio over the bands of its thresholds. Without a baseline only the bars and the acute line.
// Hover (or tap) shows the day under the pointer, found by its index.
// API:
//   dayLabels: list<string>       one per day, the last one is today
//   sessionLoads, matchLoads: list<real>   AU per day
//   acutePoints, chronicPoints, ratioPoints: var   [point], x = day index
//   loadAxisMax, loadAxisStep, ratioAxisMax: real
//   thresholds: var               { high, elevated, low, highText, elevatedText, lowText }
//   baselineReady: bool           false: no chronic line and no ratio panel
//   baselineText: string          shown instead of the ratio panel while there is no baseline
//   tooltipAt: var                function(index) → { title, rows: [{ key, label, value }] }
//   hoveredIndex: int             read-only: the day under the pointer, -1 for none
Item {
    id: root

    property list<string> dayLabels
    property list<real> sessionLoads
    property list<real> matchLoads
    property var acutePoints: []
    property var chronicPoints: []
    property var ratioPoints: []
    property real loadAxisMax: 100
    property real loadAxisStep: 100
    property real ratioAxisMax: 2
    property var thresholds: ({
            "high": 1.5,
            "elevated": 1.3,
            "low": 0.8,
            "highText": "1.5",
            "elevatedText": "1.3",
            "lowText": "0.8"
        })
    property bool baselineReady: true
    property string baselineText
    property var tooltipAt: null
    readonly property int dayCount: dayLabels.length
    readonly property int hoveredIndex: internal.hoveredIndex
    // A tick every week, the last one today, or every second week where a week is too narrow for a
    // label; whole numbers on the ratio axis (the bands and the tooltip give the exact values), so
    // it stays readable in a low panel.
    readonly property int daysPerTick: loadView.plotArea.width * 7 / Math.max(1, dayCount) < Theme.chart.minTickSpacing ? 14 : 7
    readonly property real ratioTick: 1

    // Colors and shapes of the series, shared by the legend, the series and the tooltip.
    function seriesStyle(key) {
        switch (key) {
        case "sessions":
            return {
                "color": Theme.chart.series1,
                "shape": "bar"
            };
        case "matches":
            return {
                "color": Theme.chart.series3,
                "shape": "barOutline"
            };
        case "acute":
            return {
                "color": Theme.chart.series2,
                "shape": "line"
            };
        case "chronic":
            return {
                "color": Theme.chart.series4,
                "shape": "dashedLine"
            };
        case "ratio":
            return {
                "color": Theme.chart.ratioLine,
                "shape": "line"
            };
        default:
            return {
                "color": "transparent",
                "shape": "none"
            };
        }
    }

    function legendItem(key, label) {
        const style = root.seriesStyle(key);
        return {
            "label": label,
            "color": style.color,
            "shape": style.shape
        };
    }

    function refill() {
        acuteLine.replace(root.acutePoints);
        chronicLine.replace(root.chronicPoints);
        ratioLine.replace(root.ratioPoints);
        const left = -0.5;
        const right = root.dayCount - 0.5;
        const band = (series, value) => series.replace([Qt.point(left, value), Qt.point(right, value)]);
        band(normalLower, root.thresholds.low);
        band(normalUpper, root.thresholds.elevated);
        band(elevatedLower, root.thresholds.elevated);
        band(elevatedUpper, root.thresholds.high);
        band(highLower, root.thresholds.high);
        band(highUpper, root.ratioAxisMax);
    }

    // The day under a point of a GraphsView, -1 outside its plot area.
    function dayAt(view, point) {
        const area = view.plotArea;
        if (root.dayCount === 0 || point.x < area.x || point.x >= area.x + area.width || point.y < area.y || point.y > area.y + area.height)
            return -1;
        return Math.floor((point.x - area.x) / area.width * root.dayCount);
    }

    function showDay(view, point) {
        const index = root.dayAt(view, point);
        internal.hoveredIndex = index;
        if (index < 0 || typeof root.tooltipAt !== "function") {
            tooltip.hide();
            return;
        }
        const day = root.tooltipAt(index);
        tooltip.title = day.title ?? "";
        tooltip.rows = (day.rows ?? []).map(entry => {
            const style = root.seriesStyle(entry.key);
            return {
                "key": entry.key,
                "label": entry.label,
                "value": entry.value,
                "color": style.color,
                "shape": style.shape
            };
        });
        const mapped = view.mapToItem(root, point.x, point.y);
        tooltip.showAt(mapped.x, mapped.y);
    }

    function hideDay() {
        internal.hoveredIndex = -1;
        tooltip.hide();
    }

    implicitWidth: Theme.dialogWidthL
    implicitHeight: Theme.dialogWidthM

    onAcutePointsChanged: refill()
    onChronicPointsChanged: refill()
    onRatioPointsChanged: refill()
    onThresholdsChanged: refill()
    onRatioAxisMaxChanged: refill()
    onDayCountChanged: refill()
    // The day under the pointer moves with the plot.
    onWidthChanged: hideDay()
    onHeightChanged: hideDay()
    Component.onCompleted: refill()

    QtObject {
        id: internal

        property int hoveredIndex: -1
    }

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

    component AxisLabel: Item {
        property string text
        property alias shown: label.text

        implicitWidth: label.implicitWidth
        implicitHeight: label.implicitHeight

        Label {
            id: label

            anchors.centerIn: parent
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing.s

        ChartLegend {
            Layout.fillWidth: true
            spacing: Theme.spacing.m
            items: {
                const items = [root.legendItem("sessions", qsTr("Training")), root.legendItem("matches", qsTr("Match")), root.legendItem("acute", qsTr("Acute load (7 days)"))];
                if (!root.baselineReady)
                    return items;
                items.push(root.legendItem("chronic", qsTr("Chronic load (28 days)")));
                items.push(root.legendItem("ratio", qsTr("Ratio acute:chronic")));
                items.push({
                    "label": qsTr("In range %1–%2").arg(root.thresholds.lowText).arg(root.thresholds.elevatedText),
                    "color": Theme.chart.bandNormal,
                    "borderColor": Theme.ok,
                    "shape": "band"
                });
                items.push({
                    "label": qsTr("Elevated %1–%2").arg(root.thresholds.elevatedText).arg(root.thresholds.highText),
                    "color": Theme.chart.bandElevated,
                    "borderColor": Theme.warn,
                    "shape": "band"
                });
                items.push({
                    "label": qsTr("High above %1").arg(root.thresholds.highText),
                    "color": Theme.chart.bandHigh,
                    "borderColor": Theme.danger,
                    "shape": "band"
                });
                return items;
            }
        }

        GraphsView {
            id: loadView

            objectName: "loadChartView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 2
            marginTop: Theme.spacing.s
            marginRight: Theme.spacing.s
            marginBottom: Theme.spacing.xs
            theme: ChartTheme {}

            axisX: BarCategoryAxis {
                categories: root.dayLabels
                visible: false
            }
            axisY: ValueAxis {
                id: loadAxis

                min: 0
                max: root.loadAxisMax
                tickInterval: root.loadAxisStep
                labelDecimals: 0
                subGridVisible: false
            }

            BarSeries {
                id: bars

                barsType: BarSeries.BarsType.Stacked
                barWidth: Theme.chart.barWidth

                BarSet {
                    id: sessionSet

                    label: qsTr("Training")
                    values: root.sessionLoads
                    color: root.seriesStyle("sessions").color
                    borderColor: color
                }

                BarSet {
                    id: matchSet

                    label: qsTr("Match")
                    values: root.matchLoads
                    color: root.seriesStyle("matches").color
                    borderColor: color
                }

                // A match bar is an outline, a session bar is filled.
                barDelegate: Rectangle {
                    property color barColor
                    property string barLabel

                    color: barLabel === matchSet.label ? "transparent" : barColor
                    border.width: Theme.chart.lineWidth
                    border.color: barColor
                }
            }

            LineSeries {
                id: acuteLine

                color: root.seriesStyle("acute").color
                width: Theme.chart.lineWidth
                axisY: loadAxis
                // Day indexes, aligned with the bars; ticks every week, ending today.
                axisX: ValueAxis {
                    id: dayAxis

                    min: -0.5
                    max: root.dayCount - 0.5
                    tickAnchor: root.dayCount - 1
                    tickInterval: root.daysPerTick
                    labelDecimals: 0
                    subGridVisible: false
                    labelDelegate: AxisLabel {
                        shown: root.dayLabels[Math.round(Number(text))] ?? ""
                    }
                }
            }

            LineSeries {
                id: chronicLine

                visible: root.baselineReady
                color: root.seriesStyle("chronic").color
                width: Theme.chart.lineWidth
                strokeStyle: LineSeries.StrokeStyle.DashLine
                axisX: dayAxis
                axisY: loadAxis
            }

            HoverHandler {
                onPointChanged: hovered ? root.showDay(loadView, point.position) : root.hideDay()
                onHoveredChanged: {
                    if (!hovered)
                        root.hideDay();
                }
            }

            TapHandler {
                onTapped: eventPoint => root.showDay(loadView, eventPoint.position)
            }
        }

        GraphsView {
            id: ratioView

            objectName: "ratioChartView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 1
            visible: root.baselineReady
            marginTop: Theme.spacing.s
            marginRight: Theme.spacing.s
            marginBottom: Theme.spacing.s
            theme: ChartTheme {}

            // Without labels: the day labels are under the panel above.
            axisX: ValueAxis {
                min: -0.5
                max: root.dayCount - 0.5
                visible: false
            }
            axisY: ValueAxis {
                id: ratioAxis

                min: 0
                max: root.ratioAxisMax
                tickInterval: root.ratioTick
                labelDecimals: 0
                subGridVisible: false
            }

            AreaSeries {
                color: Theme.chart.bandNormal
                borderColor: Theme.ok
                borderWidth: Theme.borderWidth
                upperSeries: LineSeries {
                    id: normalUpper
                }
                lowerSeries: LineSeries {
                    id: normalLower
                }
            }

            AreaSeries {
                color: Theme.chart.bandElevated
                borderColor: Theme.warn
                borderWidth: Theme.borderWidth
                upperSeries: LineSeries {
                    id: elevatedUpper
                }
                lowerSeries: LineSeries {
                    id: elevatedLower
                }
            }

            AreaSeries {
                color: Theme.chart.bandHigh
                borderColor: Theme.danger
                borderWidth: Theme.borderWidth
                upperSeries: LineSeries {
                    id: highUpper
                }
                lowerSeries: LineSeries {
                    id: highLower
                }
            }

            LineSeries {
                id: ratioLine

                color: root.seriesStyle("ratio").color
                width: Theme.chart.lineWidth
            }

            HoverHandler {
                onPointChanged: hovered ? root.showDay(ratioView, point.position) : root.hideDay()
                onHoveredChanged: {
                    if (!hovered)
                        root.hideDay();
                }
            }

            TapHandler {
                onTapped: eventPoint => root.showDay(ratioView, eventPoint.position)
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 1
            visible: !root.baselineReady
            text: root.baselineText
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.body
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
    }

    // The day under the pointer, across both panels.
    Rectangle {
        visible: root.hoveredIndex >= 0
        x: loadView.x + loadView.plotArea.x + (root.hoveredIndex + 0.5) * loadView.plotArea.width / Math.max(1, root.dayCount)
        y: loadView.y + loadView.plotArea.y
        width: Theme.chart.hoverMarkerWidth
        height: (ratioView.visible ? ratioView.y + ratioView.plotArea.y + ratioView.plotArea.height : loadView.y + loadView.plotArea.y + loadView.plotArea.height) - y
        color: Theme.textMuted
    }

    ChartTooltip {
        id: tooltip

        objectName: "chartTooltip"
    }
}
