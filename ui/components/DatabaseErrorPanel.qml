import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Why the database cannot be used, with the next steps: the window of DatabaseErrorView when it
// cannot be opened, and Main.qml when it opens but its first read fails (P19).
Item {
    id: root

    // Translated, from ErrorPresenter::startupMessage().
    property string message
    // Empty: the path row is hidden.
    property string databasePath
    // Shows "Try again"; the database is open and may be readable now.
    property bool canRetry: false

    signal openFolderRequested
    signal retryRequested
    signal quitRequested

    implicitWidth: Theme.errorWindow.minimumWidth
    implicitHeight: Theme.errorWindow.minimumHeight

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
                text: root.canRetry ? qsTr("The database could not be read") : qsTr("The database could not be opened")
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.h1
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.message
            textFormat: Text.PlainText
            color: Theme.text
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.databasePath.length > 0
            spacing: Theme.spacing.xs

            Label {
                Layout.fillWidth: true
                text: qsTr("Database file")
                textFormat: Text.PlainText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
            }

            Label {
                Layout.fillWidth: true
                text: root.databasePath
                textFormat: Text.PlainText
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
                text: root.canRetry ? qsTr("Try again") : qsTr("Open data folder")
                onClicked: root.canRetry ? root.retryRequested() : root.openFolderRequested()
            }

            SecondaryButton {
                visible: root.canRetry
                text: qsTr("Open data folder")
                onClicked: root.openFolderRequested()
            }

            SecondaryButton {
                text: qsTr("Quit")
                onClicked: root.quitRequested()
            }
        }
    }
}
