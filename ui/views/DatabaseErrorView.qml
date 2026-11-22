import QtQuick
import QtQuick.Controls.Basic
import FootballDSS

// Loaded instead of Main when the database cannot be opened, so it opens its own window.
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

    DatabaseErrorPanel {
        anchors.fill: parent
        focus: true
        message: StartupViewModel.errorMessage
        databasePath: StartupViewModel.databasePath
        onOpenFolderRequested: StartupViewModel.openDataFolder()
        onQuitRequested: Qt.quit()
    }
}
