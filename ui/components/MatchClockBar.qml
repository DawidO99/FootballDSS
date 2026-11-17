import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The score and the clock of Match Day (packet P13). Team names are user data: plain text.
// API:
//   teamName, opponentName: string
//   goalsFor, goalsAgainst: int
//   clockText: string       "23:41" or "45+2:10"
//   phaseText: string       "1st half"…
//   actions (default):      items on the right, e.g. the clock button
Rectangle {
    id: root

    property string teamName
    property string opponentName
    property int goalsFor: 0
    property int goalsAgainst: 0
    property string clockText: "00:00"
    property string phaseText
    default property alias actions: actionsRow.data

    implicitHeight: layout.implicitHeight + 2 * Theme.spacing.m
    radius: Theme.radius.l
    color: Theme.surface
    border.width: Theme.borderWidth
    border.color: Theme.border

    RowLayout {
        id: layout

        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.l
        anchors.rightMargin: Theme.spacing.l
        anchors.topMargin: Theme.spacing.m
        anchors.bottomMargin: Theme.spacing.m
        spacing: Theme.spacing.l

        Label {
            objectName: "teamName"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            horizontalAlignment: Text.AlignRight
            text: root.teamName
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSize.title
            font.weight: Font.DemiBold
            elide: Text.ElideLeft
        }

        Label {
            objectName: "score"
            text: root.goalsFor + " : " + root.goalsAgainst
            color: Theme.text
            font.pixelSize: Theme.matchClockFontSize
            font.weight: Font.Bold
            Accessible.name: qsTr("Score %1 to %2").arg(root.goalsFor).arg(root.goalsAgainst)
        }

        Label {
            objectName: "opponentName"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: root.opponentName
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSize.title
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        ColumnLayout {
            spacing: 0

            Label {
                objectName: "clock"
                Layout.alignment: Qt.AlignHCenter
                text: root.clockText
                color: Theme.text
                font.pixelSize: Theme.matchClockFontSize
                font.weight: Font.Bold
                // Digits of one width, so the clock does not shake.
                font.features: {
                    "tnum": 1
                }
            }

            Label {
                objectName: "phase"
                Layout.alignment: Qt.AlignHCenter
                text: root.phaseText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
            }
        }

        RowLayout {
            id: actionsRow

            spacing: Theme.spacing.s
        }
    }
}
