import QtQuick
import QtQuick.Layouts

// Placeholder until the real screen arrives.
Item {
    id: root

    signal navigateRequested(string key)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Dashboard")
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            iconName: "dashboard"
            description: qsTr("The next match, unavailable players, load signals and recent form will appear here once your squad is set up.")
            actionText: qsTr("Set up the squad")
            onActionTriggered: root.navigateRequested("squad")
        }
    }
}
