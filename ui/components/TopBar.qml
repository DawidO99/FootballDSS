pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Team name, global search (Ctrl+K), days to the next match, language and theme toggles, help (F1)
// and the current user.
Rectangle {
    id: root

    // Main.qml passes the one of the session (P15); without it there is no chip.
    property DashboardViewModel dashboard: null
    // Exposed for tests.
    readonly property alias helpDialog: help

    // Main.qml logs out, after asking about changes that are not saved.
    signal logoutRequested
    // Enter in the search field: Main.qml opens the squad with this text in its search.
    signal searchRequested(string text)
    // The chip: Main.qml opens Pre-match, or Match Day for a live match.
    signal navigateRequested(string key, var args)

    function focusSearch() {
        searchField.forceActiveFocus(Qt.ShortcutFocusReason);
        searchField.selectAll();
    }

    function openUserMenu() {
        userMenu.open();
    }

    function openHelp() {
        help.open();
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

    // P19: the accelerator of the "?" button.
    Shortcut {
        id: helpShortcut

        sequence: "F1"
        enabled: !help.visible
        onActivated: root.openHelp()
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

        AbstractButton {
            id: nextMatchChip

            objectName: "nextMatchChip"
            visible: root.dashboard !== null && root.dashboard.hasNextMatch
            implicitWidth: chipRow.implicitWidth + 2 * Theme.spacing.m
            implicitHeight: Theme.hitMin
            hoverEnabled: true
            text: root.dashboard ? root.dashboard.daysText : ""
            Accessible.role: Accessible.Button
            Accessible.name: root.dashboard ? qsTr("Next match: %1, %2").arg(root.dashboard.nextMatchOpponent).arg(text) : ""
            Keys.onReturnPressed: click()
            Keys.onEnterPressed: click()
            onClicked: root.navigateRequested(root.dashboard.nextMatchSection, {
                "matchId": root.dashboard.nextMatchId
            })

            background: Rectangle {
                radius: height / 2
                color: nextMatchChip.hovered ? Theme.rowHover : Theme.surfaceAlt
                border.width: nextMatchChip.visualFocus ? Theme.focusRingWidth : Theme.borderWidth
                border.color: nextMatchChip.visualFocus ? Theme.accent : Theme.border
            }

            contentItem: Item {
                Row {
                    id: chipRow

                    anchors.centerIn: parent
                    spacing: Theme.spacing.s

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "matchday"
                        size: Theme.iconSize.sm
                        color: root.dashboard && root.dashboard.nextMatchLive ? Theme.danger : Theme.accent
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: nextMatchChip.text
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        font.weight: Font.DemiBold
                    }
                }
            }

            PlainToolTip {
                visible: nextMatchChip.hovered
                delay: Theme.tooltipDelay
                text: root.dashboard ? qsTr("Next match: %1, %2").arg(root.dashboard.nextMatchOpponent).arg(root.dashboard.nextMatchDateText) : ""
            }
        }

        LanguageThemeSwitch {}

        IconButton {
            objectName: "helpButton"
            iconName: "help"
            iconColor: Theme.textMuted
            text: qsTr("Help (%1)").arg(helpShortcut.nativeText)
            onClicked: root.openHelp()
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

    HelpDialog {
        id: help
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
