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
            title: qsTr("Match Day")
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            iconName: "matchday"
            description: qsTr("Run the match clock, substitutions and events live during the game.")
            actionText: qsTr("Prepare the lineup")
            onActionTriggered: root.navigateRequested("prematch")
        }
    }
}
