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
            title: qsTr("Pre-match")
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            iconName: "prematch"
            description: qsTr("Pick the opponent, set up the tactics board and check the lineup.")
            actionText: qsTr("Open the squad")
            onActionTriggered: root.navigateRequested("squad")
        }
    }
}
