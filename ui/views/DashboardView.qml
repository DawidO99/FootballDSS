pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The first screen after login (SPEC section 4, packet P15): what needs attention. Read-only for
// every role; each card has its own empty or locked state. Main.qml passes the models of the
// session, which it reloads on every section change.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})
    property DashboardViewModel dashboard: null
    property CapabilityModel capabilities: null

    readonly property var loadCapability: root.capabilities ? root.capabilities.byKey["load"] ?? ({}) : ({})
    readonly property var matchesCapability: root.capabilities ? root.capabilities.byKey["matches"] ?? ({}) : ({})

    signal navigateRequested(string key, var args)

    function badgeKind(key) {
        switch (key) {
        case "ok":
        case "available":
        case "win":
            return StatusBadge.Ok;
        case "warn":
        case "locked":
        case "draw":
            return StatusBadge.Warn;
        case "danger":
        case "loss":
            return StatusBadge.Danger;
        default:
            return StatusBadge.Neutral;
        }
    }

    Connections {
        target: root.dashboard

        function onErrorOccurred(message: string) {
            Toasts.error(message);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Dashboard")
        }

        EmptyState {
            objectName: "dashboardReadError"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.dashboard !== null && root.dashboard.readError.length > 0
            iconName: "danger"
            description: root.dashboard ? root.dashboard.readError : ""
            actionText: qsTr("Retry")
            onActionTriggered: root.dashboard.reload()
        }

        FocusFlickable {
            id: page

            objectName: "dashboardPage"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.dashboard === null || root.dashboard.readError.length === 0
            contentHeight: grid.implicitHeight
            clip: true

            GridLayout {
                id: grid

                // Equal columns: a wrapping text would otherwise widen its card to its full length.
                readonly property real cardWidth: (width - (columns - 1) * columnSpacing) / columns

                width: page.width
                columns: Math.max(1, Math.min(3, Math.floor((page.width + columnSpacing) / (Theme.dashboardCardMinWidth + columnSpacing))))
                columnSpacing: Theme.spacing.l
                rowSpacing: Theme.spacing.l

                // --- Next match ---
                DashboardCard {
                    objectName: "nextMatchCard"
                    Layout.preferredWidth: grid.cardWidth
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Next match")
                    iconName: "matchday"
                    cardState: root.dashboard && root.dashboard.hasNextMatch ? "content" : "empty"
                    emptyText: root.dashboard ? root.dashboard.nextMatchEmpty.text ?? "" : ""
                    emptyActionText: root.dashboard ? root.dashboard.nextMatchEmpty.actionText ?? "" : ""
                    onEmptyActionTriggered: root.navigateRequested(root.dashboard.nextMatchEmpty.actionSection, {})

                    Label {
                        objectName: "nextMatchOpponent"
                        Layout.fillWidth: true
                        text: root.dashboard ? root.dashboard.nextMatchOpponent : ""
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.h1
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.dashboard ? root.dashboard.nextMatchDateText : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: root.dashboard ? root.dashboard.nextMatchDetails : ""
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.body
                        wrapMode: Text.WordWrap
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.s

                        StatusBadge {
                            objectName: "daysBadge"
                            kind: root.dashboard && root.dashboard.nextMatchLive ? StatusBadge.Danger : StatusBadge.Info
                            text: root.dashboard ? root.dashboard.daysText : ""
                        }

                        StatusBadge {
                            objectName: "lineupBadge"
                            visible: text.length > 0
                            kind: root.badgeKind(root.dashboard ? root.dashboard.lineupStatusKey : "")
                            text: root.dashboard ? root.dashboard.lineupText : ""
                        }
                    }

                    PrimaryButton {
                        objectName: "openNextMatch"
                        text: root.dashboard && root.dashboard.nextMatchLive ? qsTr("Open Match Day") : qsTr("Open Pre-match")
                        onClicked: root.navigateRequested(root.dashboard.nextMatchSection, {
                            "matchId": root.dashboard.nextMatchId
                        })
                    }
                }

                // --- Unavailable players ---
                DashboardCard {
                    objectName: "unavailableCard"
                    Layout.preferredWidth: grid.cardWidth
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    title: root.dashboard && root.dashboard.unavailable.length > 0 ? qsTr("Unavailable: %1").arg(root.dashboard.unavailable.length) : qsTr("Unavailable")
                    iconName: "squad"
                    cardState: root.dashboard && root.dashboard.unavailable.length > 0 ? "content" : "empty"
                    emptyText: root.dashboard ? root.dashboard.unavailableEmpty.text ?? "" : ""
                    emptyActionText: root.dashboard ? root.dashboard.unavailableEmpty.actionText ?? "" : ""
                    onEmptyActionTriggered: root.navigateRequested(root.dashboard.unavailableEmpty.actionSection, {})

                    Repeater {
                        model: root.dashboard ? root.dashboard.unavailable : []

                        delegate: DashboardRow {
                            id: absenceRow

                            required property var modelData

                            Layout.fillWidth: true
                            objectName: "unavailableRow"
                            text: absenceRow.modelData.name
                            onClicked: root.navigateRequested("squad", {
                                "playerId": absenceRow.modelData.playerId
                            })

                            PlayerStatusBadge {
                                statusKey: absenceRow.modelData.statusKey
                                text: absenceRow.modelData.typeText
                            }

                            Label {
                                Layout.fillWidth: true
                                text: absenceRow.modelData.returnText
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                objectName: "availabilityDescription"
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: absenceRow.modelData.description
                                textFormat: Text.PlainText
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                // --- Load signals ---
                DashboardCard {
                    objectName: "loadSignalsCard"
                    Layout.preferredWidth: grid.cardWidth
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Load signals")
                    iconName: "load"
                    cardState: {
                        if (root.loadCapability.stateKey === "locked")
                            return "locked";
                        if (root.dashboard && (root.dashboard.loadSignals.length > 0 || root.dashboard.busy))
                            return "content";
                        return "empty";
                    }
                    emptyText: root.dashboard ? root.dashboard.loadSignalsEmpty.text ?? "" : ""
                    emptyActionText: root.dashboard ? root.dashboard.loadSignalsEmpty.actionText ?? "" : ""
                    lockHint: root.loadCapability.hint ?? ""
                    lockActionText: root.loadCapability.actionText ?? ""
                    lockAskText: root.loadCapability.askText ?? ""
                    onEmptyActionTriggered: root.navigateRequested(root.dashboard.loadSignalsEmpty.actionSection, {})
                    onLockActionTriggered: root.navigateRequested(root.loadCapability.actionSection, {})

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Signals to talk to the player, not a diagnosis.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        objectName: "loadReading"
                        Layout.fillWidth: true
                        visible: root.dashboard !== null && root.dashboard.busy && root.dashboard.loadSignals.length === 0
                        text: qsTr("Reading the load data…")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.body
                    }

                    Repeater {
                        model: root.dashboard ? root.dashboard.loadSignals : []

                        delegate: DashboardRow {
                            id: signalRow

                            required property var modelData

                            Layout.fillWidth: true
                            objectName: "loadSignalRow"
                            text: signalRow.modelData.name
                            onClicked: root.navigateRequested("load", {
                                "playerId": signalRow.modelData.playerId
                            })

                            LoadStatusBadge {
                                statusKey: signalRow.modelData.statusKey
                                text: signalRow.modelData.statusText
                            }

                            Label {
                                Layout.fillWidth: true
                                text: signalRow.modelData.explanation
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                // --- Last five matches ---
                DashboardCard {
                    objectName: "formCard"
                    Layout.preferredWidth: grid.cardWidth
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Form: last 5 matches")
                    iconName: "goal"
                    cardState: root.matchesCapability.stateKey === "locked" ? "locked" : "content"
                    lockHint: root.matchesCapability.hint ?? ""
                    lockActionText: root.matchesCapability.actionText ?? ""
                    lockAskText: root.matchesCapability.askText ?? ""
                    onLockActionTriggered: root.navigateRequested(root.matchesCapability.actionSection, {})

                    Label {
                        objectName: "formSummary"
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: root.dashboard ? root.dashboard.formSummary : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: root.dashboard ? root.dashboard.form : []

                        delegate: DashboardRow {
                            id: formRow

                            required property var modelData

                            Layout.fillWidth: true
                            objectName: "formRow"
                            text: qsTr("%1 %2").arg(formRow.modelData.scoreText).arg(formRow.modelData.opponent)
                            Accessible.name: qsTr("%1, %2 %3").arg(formRow.modelData.resultText).arg(formRow.modelData.scoreText).arg(formRow.modelData.opponent)
                            onClicked: root.navigateRequested("matchreport", {
                                "matchId": formRow.modelData.matchId
                            })

                            StatusBadge {
                                objectName: "formResult"
                                kind: root.badgeKind(formRow.modelData.resultKey)
                                text: qsTr("%1 · %2").arg(formRow.modelData.letter).arg(formRow.modelData.resultText)
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("%1 · %2").arg(formRow.modelData.dateText).arg(formRow.modelData.ratingText)
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                // --- Today's sessions ---
                DashboardCard {
                    objectName: "todayCard"
                    Layout.preferredWidth: grid.cardWidth
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Today's training")
                    iconName: "training"
                    cardState: root.dashboard && root.dashboard.todaySessions.length > 0 ? "content" : "empty"
                    emptyText: root.dashboard ? root.dashboard.todayEmpty.text ?? "" : ""
                    emptyActionText: root.dashboard ? root.dashboard.todayEmpty.actionText ?? "" : ""
                    onEmptyActionTriggered: root.navigateRequested(root.dashboard.todayEmpty.actionSection, {})

                    Repeater {
                        model: root.dashboard ? root.dashboard.todaySessions : []

                        delegate: DashboardRow {
                            id: sessionRow

                            required property var modelData

                            Layout.fillWidth: true
                            objectName: "todaySessionRow"
                            text: qsTr("%1 · %2").arg(sessionRow.modelData.timeText).arg(sessionRow.modelData.typeText)
                            onClicked: root.navigateRequested("training", {
                                "sessionId": sessionRow.modelData.sessionId
                            })

                            Label {
                                Layout.fillWidth: true
                                text: sessionRow.modelData.durationText
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                            }
                        }
                    }
                }

                // --- Features (SPEC section 2) ---
                DashboardCard {
                    objectName: "featuresCard"
                    Layout.preferredWidth: grid.cardWidth
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Features")
                    iconName: "info"

                    Repeater {
                        model: root.capabilities

                        delegate: ColumnLayout {
                            id: featureRow

                            required property string key
                            required property string title
                            required property string stateKey
                            required property string stateText
                            required property string description
                            required property string hint
                            required property string actionSection
                            required property string actionText
                            required property string askText

                            objectName: "feature_" + featureRow.key
                            Layout.fillWidth: true
                            spacing: Theme.spacing.xs

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacing.s

                                Label {
                                    Layout.fillWidth: true
                                    text: featureRow.title
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSize.body
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                StatusBadge {
                                    kind: root.badgeKind(featureRow.stateKey)
                                    text: featureRow.stateText
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: featureRow.description
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: featureRow.hint
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: featureRow.askText
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                                font.italic: true
                                wrapMode: Text.WordWrap
                            }

                            SecondaryButton {
                                visible: text.length > 0
                                text: featureRow.actionText
                                onClicked: root.navigateRequested(featureRow.actionSection, {})
                            }
                        }
                    }
                }
            }
        }
    }
}
