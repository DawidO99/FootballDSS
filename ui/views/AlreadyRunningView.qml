import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Loaded instead of Main when FootballDSS is already running (P19), so it opens its own window.
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
                name: "info"
                color: Theme.info
                size: Theme.fontSize.h1
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("FootballDSS is already running")
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.h1
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Switch to its window, for example from the taskbar. Only one FootballDSS can be open at a time, so that two windows never change the same data.")
            textFormat: Text.PlainText
            color: Theme.text
            wrapMode: Text.WordWrap
        }

        Item {
            Layout.fillHeight: true
        }

        PrimaryButton {
            Layout.alignment: Qt.AlignRight
            focus: true
            text: qsTr("OK")
            onClicked: Qt.quit()
        }
    }
}
