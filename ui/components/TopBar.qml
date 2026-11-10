pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Team name, global search (Ctrl+K), language and theme toggles, current user.
Rectangle {
    id: root

    // Main.qml logs out, after asking about changes that are not saved.
    signal logoutRequested
    // Enter in the search field: Main.qml opens the squad with this text in its search.
    signal searchRequested(string text)

    function focusSearch() {
        searchField.forceActiveFocus(Qt.ShortcutFocusReason);
        searchField.selectAll();
    }

    function openUserMenu() {
        userMenu.open();
    }

    function openChangePinDialog() {
        changePinDialog.openEmpty();
    }

    implicitHeight: Theme.topBarHeight
    color: Theme.surface

    Shortcut {
        id: searchShortcut

        sequence: "Ctrl+K"
        onActivated: root.focusSearch()
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.borderWidth
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.xl
        anchors.rightMargin: Theme.spacing.l
        spacing: Theme.spacing.l

        Label {
            text: AppState.teamName
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSize.title
            font.weight: Font.DemiBold
        }

        TextField {
            id: searchField

            objectName: "globalSearch"
            Layout.preferredWidth: Theme.topBarSearchWidth
            implicitHeight: Theme.hitMin + Theme.spacing.xs
            leftPadding: Theme.spacing.s + searchIcon.width + Theme.spacing.s
            color: Theme.text
            placeholderText: qsTr("Search… (%1)").arg(searchShortcut.nativeText)
            placeholderTextColor: Theme.textMuted
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            font.pixelSize: Theme.fontSize.body
            Accessible.name: qsTr("Search")
            onAccepted: {
                const query = text.trim();
                if (query.length === 0)
                    return;
                text = "";
                focus = false;
                root.searchRequested(query);
            }
            Keys.onEscapePressed: {
                text = "";
                focus = false;
            }

            background: Rectangle {
                radius: Theme.radius.m
                color: Theme.surfaceAlt
                border.width: searchField.activeFocus ? Theme.focusRingWidth : Theme.borderWidth
                border.color: searchField.activeFocus ? Theme.accent : Theme.border
            }

            Icon {
                id: searchIcon

                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.s
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: Theme.iconSize.sm
                color: Theme.textMuted
            }
        }

        Item {
            Layout.fillWidth: true
        }

        LanguageThemeSwitch {}

        AbstractButton {
            id: userChip

            implicitWidth: userRow.implicitWidth + 2 * Theme.spacing.m
            implicitHeight: Theme.hitMin
            hoverEnabled: true
            text: qsTr("%1, %2").arg(AppState.userName).arg(AppState.roleName)
            Accessible.role: Accessible.ButtonMenu
            Accessible.name: text
            Keys.onReturnPressed: click()
            Keys.onEnterPressed: click()
            onClicked: root.openUserMenu()

            background: Rectangle {
                radius: height / 2
                color: userChip.hovered ? Theme.rowHover : Theme.surfaceAlt
                border.width: userChip.visualFocus ? Theme.focusRingWidth : Theme.borderWidth
                border.color: userChip.visualFocus ? Theme.accent : Theme.border
            }

            contentItem: Item {
                Row {
                    id: userRow

                    anchors.centerIn: parent
                    spacing: Theme.spacing.s

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "user"
                        size: Theme.iconSize.sm
                        color: Theme.textMuted
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: AppState.userName
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: AppState.roleName
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                    }

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevronDown"
                        size: Theme.iconSize.s
                        color: Theme.textMuted
                    }
                }
            }

            Menu {
                id: userMenu

                x: userChip.width - width
                y: userChip.height + Theme.spacing.xs

                MenuItem {
                    text: qsTr("Change PIN")
                    onTriggered: root.openChangePinDialog()
                }

                MenuItem {
                    text: qsTr("Log out")
                    onTriggered: root.logoutRequested()
                }
            }
        }
    }

    ChangePinViewModel {
        id: changePin

        onErrorOccurred: message => Toasts.error(message)
        onLockedChanged: {
            if (changePin.locked)
                currentPinInput.clear();
            else if (changePinDialog.visible)
                currentPinInput.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    AppDialog {
        id: changePinDialog

        objectName: "changePinDialog"

        function openEmpty() {
            changePin.clearErrors();
            currentPinInput.clear();
            newPinInput.clear();
            pinRepeatInput.clear();
            changePinDialog.open();
            currentPinInput.forceActiveFocus(Qt.TabFocusReason);
        }

        size: AppDialog.Small
        title: qsTr("Change PIN")
        primaryText: qsTr("Change PIN")
        primaryEnabled: !changePin.locked
        onPrimaryClicked: {
            if (changePin.change(currentPinInput.text, newPinInput.text, pinRepeatInput.text)) {
                changePinDialog.close();
                Toasts.info(qsTr("Your PIN was changed."));
            }
        }

        FormField {
            id: currentPinFormField

            Layout.fillWidth: true
            label: qsTr("Current PIN")
            mandatory: true
            error: currentPinInput.effectiveError

            PinField {
                id: currentPinInput

                objectName: "currentPinInput"
                Layout.fillWidth: true
                enabled: !changePin.locked
                error: changePin.currentPinError
                Accessible.name: currentPinFormField.label
            }
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

                Layout.fillWidth: true
                error: changePin.pinRepeatError
                Accessible.name: pinRepeatFormField.label
            }
        }
    }
}
