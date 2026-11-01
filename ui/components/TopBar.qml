pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Team name, global search (Ctrl+K), language and theme toggles, current user.
Rectangle {
    id: root

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

    implicitHeight: 56
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
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing.xl
        anchors.rightMargin: Theme.spacing.l
        spacing: Theme.spacing.l

        Label {
            text: AppState.teamName
            color: Theme.text
            font.pixelSize: Theme.fontSize.title
            font.weight: Font.DemiBold
        }

        TextField {
            id: searchField

            Layout.preferredWidth: 360
            implicitHeight: Theme.hitMin + Theme.spacing.xs
            leftPadding: Theme.spacing.s + searchIcon.width + Theme.spacing.s
            color: Theme.text
            placeholderText: qsTr("Search… (%1)").arg(searchShortcut.nativeText)
            placeholderTextColor: Theme.textMuted
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            font.pixelSize: Theme.fontSize.body
            Accessible.name: qsTr("Search")
            Keys.onEscapePressed: {
                text = "";
                focus = false;
            }

            background: Rectangle {
                radius: Theme.radius.m
                color: Theme.surfaceAlt
                border.width: searchField.activeFocus ? 2 : 1
                border.color: searchField.activeFocus ? Theme.accent : Theme.border
            }

            Icon {
                id: searchIcon

                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing.s
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 18
                color: Theme.textMuted
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Row {
            spacing: Theme.spacing.xs

            Repeater {
                model: LanguageManager.availableLanguages

                delegate: AbstractButton {
                    id: languageButton

                    required property string modelData
                    readonly property bool selected: LanguageManager.language === modelData

                    implicitWidth: Math.max(Theme.hitMin + Theme.spacing.s, implicitContentWidth)
                    implicitHeight: Theme.hitMin
                    text: modelData.toUpperCase()
                    hoverEnabled: true
                    Accessible.name: qsTr("Interface language: %1").arg(Qt.locale(modelData).nativeLanguageName)
                    Keys.onReturnPressed: click()
                    Keys.onEnterPressed: click()
                    onClicked: LanguageManager.setLanguage(modelData)

                    background: Rectangle {
                        radius: Theme.radius.s
                        color: languageButton.selected ? Theme.accent : languageButton.hovered ? Theme.surfaceAlt : "transparent"
                        border.width: languageButton.visualFocus ? 2 : 1
                        border.color: languageButton.visualFocus ? Theme.text : languageButton.selected ? Theme.accent : Theme.border
                    }

                    contentItem: Label {
                        text: languageButton.text
                        color: languageButton.selected ? Theme.accentText : Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        AbstractButton {
            id: themeToggle

            implicitWidth: Theme.hitMin + Theme.spacing.xs
            implicitHeight: Theme.hitMin + Theme.spacing.xs
            hoverEnabled: true
            text: AppSettings.darkTheme ? qsTr("Switch to light theme") : qsTr("Switch to dark theme")
            Accessible.name: text
            ToolTip.visible: hovered
            ToolTip.text: text
            ToolTip.delay: 400
            Keys.onReturnPressed: click()
            Keys.onEnterPressed: click()
            onClicked: AppSettings.darkTheme = !AppSettings.darkTheme

            background: Rectangle {
                radius: Theme.radius.m
                color: themeToggle.hovered ? Theme.surfaceAlt : "transparent"
                border.width: themeToggle.visualFocus ? 2 : 0
                border.color: Theme.accent
            }

            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    name: AppSettings.darkTheme ? "sun" : "moon"
                    color: Theme.text
                }
            }
        }

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
                        size: 18
                        color: Theme.textMuted
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: AppState.userName
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
                    onTriggered: AppState.logout()
                }
            }
        }
    }

    ChangePinViewModel {
        id: changePin

        onErrorOccurred: message => Toasts.error(message)
    }

    AppDialog {
        id: changePinDialog

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

                Layout.fillWidth: true
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
