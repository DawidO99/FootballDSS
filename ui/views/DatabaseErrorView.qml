import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Loaded instead of Main when the database cannot be opened, so it opens its own window.
ApplicationWindow {
    id: window

    width: 720
    height: 420
    minimumWidth: 560
    minimumHeight: 360
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
                name: "danger"
                color: Theme.danger
                size: Theme.fontSize.h1
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("The database could not be opened")
                color: Theme.text
                font.pixelSize: Theme.fontSize.h1
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }

        Label {
            Layout.fillWidth: true
            text: StartupViewModel.errorMessage
            color: Theme.text
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: StartupViewModel.databasePath.length > 0
            spacing: Theme.spacing.xs

            Label {
                Layout.fillWidth: true
                text: qsTr("Database file")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
            }

            Label {
                Layout.fillWidth: true
                text: StartupViewModel.databasePath
                color: Theme.text
                wrapMode: Text.WrapAnywhere
            }
        }

        Item {
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing.m

            PrimaryButton {
                focus: true
                text: qsTr("Open data folder")
                onClicked: StartupViewModel.openDataFolder()
            }

            PrimaryButton {
                text: qsTr("Quit")
                onClicked: Qt.quit()
            }
        }
    }
}
