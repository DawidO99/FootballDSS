import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Loaded instead of Main when the start is refused (P19), so it opens its own window: another
// FootballDSS is running, or the lock file in the data folder cannot be written, so nothing could
// tell a second start.
ApplicationWindow {
    id: window

    width: Theme.errorWindow.width
    height: Theme.errorWindow.height
    minimumWidth: Theme.errorWindow.minimumWidth
    minimumHeight: Theme.errorWindow.minimumHeight
    visible: true
    title: qsTr("FootballDSS")
    color: Theme.bg
    font.pixelSize: Theme.fontSize.body

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xxl
        spacing: Theme.spacing.l

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            Icon {
                name: StartupViewModel.otherInstanceRunning ? "info" : "danger"
                color: StartupViewModel.otherInstanceRunning ? Theme.info : Theme.danger
                size: Theme.fontSize.h1
            }

            Label {
                objectName: "refusalTitle"
                Layout.fillWidth: true
                text: StartupViewModel.otherInstanceRunning ? qsTr("FootballDSS is already running") : qsTr("FootballDSS cannot start")
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.h1
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }

        Label {
            objectName: "refusalMessage"
            Layout.fillWidth: true
            text: StartupViewModel.otherInstanceRunning ? qsTr("Switch to its window, for example from the taskbar. Only one FootballDSS can be open at a time, so that two windows never change the same data.") : qsTr("The data folder cannot be written, so FootballDSS cannot make sure that only one copy runs. Check that the folder is not read-only and that the disk is not full, then start FootballDSS again.")
            textFormat: Text.PlainText
            color: Theme.text
            wrapMode: Text.WordWrap
        }

        Item {
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing.m

            SecondaryButton {
                visible: !StartupViewModel.otherInstanceRunning
                text: qsTr("Open data folder")
                onClicked: StartupViewModel.openDataFolder()
            }

            PrimaryButton {
                focus: true
                text: qsTr("OK")
                onClicked: Qt.quit()
            }
        }
    }
}
