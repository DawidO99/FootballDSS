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
            title: qsTr("Load")
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            iconName: "load"
            description: qsTr("Signals to talk to the player appear after a few weeks of RPE entries.")
            actionText: qsTr("Record a training session")
            onActionTriggered: root.navigateRequested("training")
        }
    }
}
