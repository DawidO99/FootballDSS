import QtQuick
import QtQuick.Layouts

// Placeholder until the real screen arrives.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})

    signal navigateRequested(string key, var args)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Import")
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            iconName: "import"
            description: qsTr("Import squads, matches and training data from CSV or XLSX files.")
            actionText: qsTr("Back to the dashboard")
            onActionTriggered: root.navigateRequested("dashboard", {})
        }
    }
}
