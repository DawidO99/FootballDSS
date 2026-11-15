pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The load of one player (packet P11): status, why, what is missing, the numbers and LoadChart.
// The Load screen and the "Load" tab of the player card show the same panel.
// API:
//   playerId: double          0: nothing
//   showFigures: bool         the row of numbers; off where a table beside it shows them
//   viewModel                 read-only: its PlayerLoadViewModel
//   reload()
//   navigateRequested(string key, var args)   the empty state leads to the training sheet
ColumnLayout {
    id: root

    property double playerId: 0
    property bool showFigures: true
    readonly property alias viewModel: load
    readonly property bool canEnterRpe: AppState.can("EditTraining")

    signal navigateRequested(string key, var args)

    function reload() {
        load.reload();
    }

    spacing: Theme.spacing.m

    PlayerLoadViewModel {
        id: load

        playerId: root.playerId
    }

    RowLayout {
        Layout.fillWidth: true
        visible: load.hasData || load.busy
        spacing: Theme.spacing.m

        LoadStatusBadge {
            objectName: "playerLoadStatus"
            visible: load.hasData
            statusKey: load.statusKey
            text: load.statusText
        }

        Label {
            Layout.fillWidth: true
            text: load.busy ? qsTr("Calculating the load…") : load.asOfText
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
            elide: Text.ElideRight
        }
    }

    Label {
        objectName: "playerLoadExplanation"
        Layout.fillWidth: true
        visible: load.hasData && text.length > 0
        text: load.explanation
        textFormat: Text.PlainText
        color: Theme.text
        font.pixelSize: Theme.fontSize.body
        wrapMode: Text.Wrap
    }

    Label {
        Layout.fillWidth: true
        visible: load.hasData && text.length > 0
        text: load.hint
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.body
        wrapMode: Text.Wrap
    }

    RowLayout {
        Layout.fillWidth: true
        visible: load.missingRpeText.length > 0
        spacing: Theme.spacing.s

        Icon {
            Layout.alignment: Qt.AlignTop
            name: "warn"
            size: Theme.iconSize.s
            color: Theme.warn
        }

        Label {
            Layout.fillWidth: true
            text: load.missingRpeText
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }

    Flow {
        Layout.fillWidth: true
        visible: load.hasData && root.showFigures
        spacing: Theme.spacing.l

        Repeater {
            model: [
                {
                    "label": qsTr("Acute load"),
                    "value": load.acuteText
                },
                {
                    "label": qsTr("Chronic load"),
                    "value": load.chronicText
                },
                {
                    "label": qsTr("Ratio"),
                    "value": load.ratioText
                },
                {
                    "label": qsTr("Monotony"),
                    "value": load.monotonyText
                },
                {
                    "label": qsTr("Strain"),
                    "value": load.strainText
                },
                {
                    "label": qsTr("Last 7 days"),
                    "value": load.weekLoadText
                }
            ].filter(entry => entry.value.length > 0)

            delegate: Column {
                id: figure

                required property var modelData

                Label {
                    text: figure.modelData.label
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                }

                Label {
                    text: figure.modelData.value
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.title
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    LoadChart {
        id: chart

        objectName: "loadChart"
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: load.hasData
        dayLabels: load.chartDayLabels
        sessionLoads: load.sessionLoads
        matchLoads: load.matchLoads
        acutePoints: load.acutePoints
        chronicPoints: load.chronicPoints
        ratioPoints: load.ratioPoints
        loadAxisMax: load.loadAxisMax
        loadAxisStep: load.loadAxisStep
        ratioAxisMax: load.ratioAxisMax
        thresholds: load.thresholds
        baselineReady: load.baselineReady
        baselineText: qsTr("Building the baseline: the chronic load and the ratio appear with enough data.")
        tooltipAt: index => load.dayAt(index)
    }

    EmptyState {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: root.playerId !== 0 && !load.hasData && !load.busy && load.loadError.length === 0
        iconName: "load"
        title: qsTr("No RPE for this player yet")
        description: root.canEnterRpe ? qsTr("The load appears when RPE is entered for this player in a training session.") : qsTr("The load appears when RPE is entered for this player in a training session. Ask the coach, the assistant or the physio to enter it.")
        actionText: root.canEnterRpe ? qsTr("Record a training session") : ""
        onActionTriggered: root.navigateRequested("training", {})
    }

    EmptyState {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: load.loadError.length > 0
        iconName: "load"
        title: qsTr("The load could not be read")
        description: load.loadError
        actionText: qsTr("Try again")
        onActionTriggered: load.reload()
    }
}
