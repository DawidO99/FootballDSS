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
            title: qsTr("Training")
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            iconName: "training"
            description: qsTr("After each session you will record attendance and RPE for the whole squad here.")
            actionText: qsTr("Open the calendar")
            onActionTriggered: root.navigateRequested("calendar")
        }
    }
}
