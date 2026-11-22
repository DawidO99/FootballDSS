import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// After a login with a PIN a Coach chose (a new user or a reset PIN): the user sets their own PIN
// before anything else (P19 decision 5). Enter saves; logging out goes back to the profile picker.
FocusScope {
    id: root

    // Exposed for tests.
    readonly property ChangePinViewModel viewModel: changePin

    function submit() {
        if (changePin.setRequired(newPinInput.text, pinRepeatInput.text)) {
            AppState.refresh();
            Toasts.info(qsTr("Your PIN was changed."));
        } else if (changePin.newPinError.length > 0) {
            newPinInput.forceActiveFocus(Qt.TabFocusReason);
        } else if (changePin.pinRepeatError.length > 0) {
            pinRepeatInput.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    Keys.onReturnPressed: root.submit()
    Keys.onEnterPressed: root.submit()
    Component.onCompleted: newPinInput.forceActiveFocus(Qt.TabFocusReason)

    ChangePinViewModel {
        id: changePin

        onErrorOccurred: message => Toasts.error(message)
    }

    LanguageThemeSwitch {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacing.m
        z: 1
    }

    Card {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spacing.xl, Theme.dialogWidthS)
        title: qsTr("Set your own PIN")

        Label {
            Layout.fillWidth: true
            text: qsTr("%1, your PIN was set by a coach. Choose a new PIN that only you know.").arg(AppState.userName)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.text
        }

        FormField {
            id: newPinFormField

            Layout.fillWidth: true
            label: qsTr("New PIN")
            mandatory: true
            helpText: qsTr("4 to 8 digits.")
            error: newPinInput.effectiveError

            PinField {
                id: newPinInput

                objectName: "requiredNewPinInput"
                Layout.fillWidth: true
                error: changePin.newPinError
                Accessible.name: newPinFormField.label
            }
        }

        FormField {
            id: pinRepeatFormField

            Layout.fillWidth: true
            label: qsTr("Repeat the PIN")
            mandatory: true
            error: pinRepeatInput.effectiveError

            PinField {
                id: pinRepeatInput

                objectName: "requiredPinRepeatInput"
                Layout.fillWidth: true
                error: changePin.pinRepeatError
                Accessible.name: pinRepeatFormField.label
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            SecondaryButton {
                objectName: "requiredPinLogoutButton"
                text: qsTr("Log out")
                onClicked: AppState.logout()
            }

            Item {
                Layout.fillWidth: true
            }

            PrimaryButton {
                objectName: "requiredPinSaveButton"
                text: qsTr("Save PIN")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("%1 (Enter)").arg(text)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.submit()
            }
        }
    }
}
