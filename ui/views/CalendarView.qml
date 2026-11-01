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
            title: qsTr("Calendar")
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            iconName: "calendar"
            description: qsTr("Matches and training sessions will be listed here.")
            actionText: qsTr("Import a schedule")
            onActionTriggered: root.navigateRequested("import", {})
        }
    }
}
