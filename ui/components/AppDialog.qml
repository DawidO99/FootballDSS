import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Modal dialog with a header, body slot and footer buttons (optional danger on the left,
// secondary + primary on the right). Enter triggers the primary button (which does NOT close
// the dialog automatically so the caller can validate first); Esc and Cancel reject the dialog.
// A body too high for the window scrolls; the header and the footer stay in view.
// API:
//   title: string           (inherited from Dialog)
//   size: int               AppDialog.Small / Medium / Large
//   primaryText: string     default qsTr("Save")
//   primaryEnabled: bool    default true
//   secondaryText: string   default qsTr("Cancel")
//   dangerText: string      empty = hidden
//   touch: bool             44 px buttons (Match Day, tactics board), default false
//   body (default):         items placed in the dialog content column
//   primaryClicked(), dangerClicked(), rejected()
Dialog {
    id: root

    enum Size {
        Small,
        Medium,
        Large
    }

    property int size: AppDialog.Medium
    property string primaryText: qsTr("Save")
    property bool primaryEnabled: true
    property string secondaryText: qsTr("Cancel")
    property string dangerText
    property bool touch: false
    default property alias body: bodyColumn.data

    signal primaryClicked
    signal dangerClicked

    function triggerPrimary() {
        if (root.primaryText.length > 0 && root.primaryEnabled)
            root.primaryClicked();
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: size === AppDialog.Small ? Theme.dialogWidthS : size === AppDialog.Large ? Theme.dialogWidthL : Theme.dialogWidthM
    // What does not fit the window scrolls between the header and the footer, so the buttons stay.
    height: parent ? Math.min(implicitHeight, parent.height - 2 * Theme.spacing.m) : implicitHeight
    padding: Theme.spacing.xl
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius.l
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    header: Item {
        implicitHeight: headerRow.implicitHeight + Theme.spacing.xl + Theme.spacing.m

        RowLayout {
            id: headerRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.spacing.xl
            anchors.rightMargin: Theme.spacing.l
            anchors.topMargin: Theme.spacing.l
            spacing: Theme.spacing.m

            Label {
                Layout.fillWidth: true
                text: root.title
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.title
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            IconButton {
                objectName: "dialogCloseButton"
                touch: root.touch
                iconName: "close"
                iconColor: Theme.textMuted
                text: qsTr("Close")
                onClicked: root.reject()
            }
        }
    }

    contentItem: FocusScope {
        implicitWidth: bodyColumn.implicitWidth
        implicitHeight: bodyColumn.implicitHeight
        focus: true
        Keys.onReturnPressed: root.triggerPrimary()
        Keys.onEnterPressed: root.triggerPrimary()

        FocusFlickable {
            id: bodyArea

            objectName: "dialogBody"
            anchors.fill: parent
            contentHeight: bodyColumn.implicitHeight

            ColumnLayout {
                id: bodyColumn

                width: bodyArea.width
                spacing: Theme.spacing.m
            }
        }
    }

    footer: Item {
        implicitHeight: footerRow.implicitHeight + Theme.spacing.xl + Theme.spacing.m

        RowLayout {
            id: footerRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.spacing.xl
            anchors.rightMargin: Theme.spacing.xl
            anchors.bottomMargin: Theme.spacing.l
            spacing: Theme.spacing.m

            DangerButton {
                objectName: "dialogDangerButton"
                touch: root.touch
                visible: root.dangerText.length > 0
                text: root.dangerText
                onClicked: root.dangerClicked()
            }

            Item {
                Layout.fillWidth: true
            }

            SecondaryButton {
                objectName: "dialogSecondaryButton"
                touch: root.touch
                visible: root.secondaryText.length > 0
                text: root.secondaryText
                onClicked: root.reject()
            }

            PrimaryButton {
                objectName: "dialogPrimaryButton"
                touch: root.touch
                visible: root.primaryText.length > 0
                enabled: root.primaryEnabled
                text: root.primaryText
                onClicked: root.primaryClicked()
            }
        }
    }
}

