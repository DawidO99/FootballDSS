pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The match rules and the Staff section, for those who may manage settings and staff; the modules
// preference of this computer for every role (P15 decision 4). Other settings arrive with later
// packets.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})
    readonly property bool canManageStaff: AppState.can("ManageStaff")
    readonly property bool canManageSettings: AppState.can("ManageSettings")
    // Exposed for tests: null without ManageStaff.
    readonly property Item staffSection: staffLoader.item as Item

    signal navigateRequested(string key, var args)

    Connections {
        target: AppSettings

        function onErrorOccurred(message: string) {
            Toasts.error(message);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Settings")
        }

        // P12 decision 3: one threshold for every competition, kept on this computer.
        Card {
            objectName: "matchRulesCard"
            Layout.fillWidth: true
            visible: root.canManageSettings
            title: qsTr("Match rules")

            FormField {
                id: yellowCardsFormField

                Layout.fillWidth: true
                label: qsTr("Yellow cards before a suspension")
                helpText: qsTr("Pre-match warns when a player is one yellow card away from a suspension in the competition of the match.")

                NumberField {
                    id: yellowCardThresholdField

                    objectName: "yellowCardThresholdField"
                    from: AppSettings.minYellowCardThreshold
                    to: AppSettings.maxYellowCardThreshold
                    value: AppSettings.yellowCardThreshold
                    Accessible.name: yellowCardsFormField.label
                    // A refused change shows the stored value again.
                    onValueModified: value => {
                        if (value !== null && !AppSettings.setYellowCardThreshold(value))
                            yellowCardThresholdField.showValue();
                    }
                }
            }
        }

        // P15 decision 4: a preference of this computer, like the theme; every role may change it.
        Card {
            objectName: "modulesCard"
            Layout.fillWidth: true
            title: qsTr("Modules")

            FormField {
                Layout.fillWidth: true
                label: qsTr("Advanced modules")
                helpText: qsTr("Pro analysis appears once event data is imported. On: it is listed on the dashboard before that, with what to import.")

                CheckBoxField {
                    objectName: "showAdvancedModules"
                    text: qsTr("Show advanced modules")
                    checked: AppSettings.showAdvancedModules
                    onToggled: AppSettings.showAdvancedModules = checked
                }
            }
        }

        Loader {
            id: staffLoader

            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.canManageStaff
            visible: active
            sourceComponent: staffComponent
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.canManageStaff && !root.canManageSettings
            iconName: "settings"
            description: qsTr("Club details, custom metrics and backups will be configured here.")
            actionText: qsTr("Back to the dashboard")
            onActionTriggered: root.navigateRequested("dashboard", {})
        }
    }

    Component {
        id: staffComponent

        Card {
            id: section

            // Exposed for tests.
            readonly property StaffViewModel viewModel: staffModel
            property var selectedUser: ({})
            readonly property bool hasSelection: selectedUser.staffId !== undefined
            readonly property bool ownRowSelected: selectedUser.staffId === staffModel.currentUserId

            function syncSelection() {
                section.selectedUser = staffModel.staff.rowAt(staffTable.currentIndex);
            }

            function openAddDialog() {
                editDialog.editedId = 0;
                editDialog.openWith("", "assistant");
            }

            function openEditDialog() {
                if (!section.hasSelection)
                    return;
                editDialog.editedId = section.selectedUser.staffId;
                editDialog.openWith(section.selectedUser.name, section.selectedUser.roleKey);
            }

            function openResetPinDialog() {
                if (section.hasSelection)
                    resetPinDialog.openFor(section.selectedUser.staffId, section.selectedUser.name);
            }

            function deactivateSelected() {
                const id = section.selectedUser.staffId;
                const name = section.selectedUser.name;
                if (staffModel.deactivate(id))
                    Toasts.show(qsTr("%1 was deactivated.").arg(name), qsTr("Undo"), () => staffModel.reactivate(id));
            }

            function reactivateSelected() {
                staffModel.reactivate(section.selectedUser.staffId);
            }

            title: qsTr("Staff")
            Component.onCompleted: section.syncSelection()

            actions: PrimaryButton {
                text: qsTr("Add staff member")
                onClicked: section.openAddDialog()
            }

            StaffViewModel {
                id: staffModel

                onErrorOccurred: message => Toasts.error(message)
                onStaffChanged: {
                    // The list was read anew: the same person stays selected.
                    staffTable.currentIndex = staffModel.staff.rowOf(section.selectedUser.staffId ?? 0);
                    section.syncSelection();
                    // May rename the session user or end the session, which removes this view.
                    AppState.refresh();
                }
            }

            DataTable {
                id: staffTable

                Layout.fillWidth: true
                Layout.fillHeight: true
                Accessible.name: qsTr("Staff")
                model: staffModel.staff
                columns: [
                    {
                        "role": "name",
                        "title": qsTr("Name"),
                        "width": 0.5,
                        "sortable": false
                    },
                    {
                        "role": "roleName",
                        "title": qsTr("Role"),
                        "width": 0.3,
                        "sortable": false
                    },
                    {
                        "role": "active",
                        "title": qsTr("Status"),
                        "width": 0.2,
                        "sortable": false,
                        "delegate": activeCell
                    }
                ]
                emptyState: staffEmptyState
                onCurrentIndexChanged: section.syncSelection()
                onActivated: section.openEditDialog()
            }

            Component {
                id: activeCell

                DataTableCell {
                    id: cell

                    StatusBadge {
                        anchors.verticalCenter: parent.verticalCenter
                        kind: cell.value ? StatusBadge.Ok : StatusBadge.Info
                        text: cell.value ? qsTr("Active") : qsTr("Deactivated")
                    }
                }
            }

            Component {
                id: staffEmptyState

                EmptyState {
                    iconName: "user"
                    title: qsTr("The staff list could not be read")
                    description: staffModel.loadError
                    actionText: qsTr("Try again")
                    onActionTriggered: staffModel.reload()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.m

                SecondaryButton {
                    enabled: section.hasSelection
                    text: qsTr("Edit")
                    onClicked: section.openEditDialog()
                }

                SecondaryButton {
                    enabled: section.hasSelection
                    text: qsTr("Reset PIN")
                    onClicked: section.openResetPinDialog()
                }

                // A disabled button gets no hover, so the wrapper shows why it is disabled.
                Item {
                    visible: !section.hasSelection || section.selectedUser.active
                    implicitWidth: deactivateButton.implicitWidth
                    implicitHeight: deactivateButton.implicitHeight

                    HoverHandler {
                        id: deactivateHover
                    }

                    DangerButton {
                        id: deactivateButton

                        anchors.fill: parent
                        enabled: section.hasSelection && !section.ownRowSelected
                        text: qsTr("Deactivate")
                        onClicked: section.deactivateSelected()
                    }

                    ToolTip.visible: deactivateHover.hovered && section.ownRowSelected
                    ToolTip.text: qsTr("You cannot deactivate your own account. Another coach has to do it.")
                    ToolTip.delay: Theme.tooltipDelay
                }

                SecondaryButton {
                    visible: section.hasSelection && !section.selectedUser.active
                    text: qsTr("Reactivate")
                    onClicked: section.reactivateSelected()
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            AppDialog {
                id: editDialog

                // 0: adding a new staff member.
                property double editedId: 0
                property string roleKey
                readonly property bool adding: editedId === 0

                function openWith(name, role) {
                    staffModel.clearErrors();
                    nameInput.text = name;
                    editDialog.roleKey = role;
                    newPinInput.clear();
                    newPinRepeatInput.clear();
                    editDialog.open();
                    nameInput.forceActiveFocus(Qt.TabFocusReason);
                }

                title: adding ? qsTr("Add staff member") : qsTr("Edit staff member")
                onPrimaryClicked: {
                    const done = editDialog.adding ? staffModel.add(nameInput.text, editDialog.roleKey, newPinInput.text, newPinRepeatInput.text) : staffModel.update(editDialog.editedId, nameInput.text, editDialog.roleKey);
                    if (done)
                        editDialog.close();
                }

                FormField {
                    id: nameFormField

                    Layout.fillWidth: true
                    label: qsTr("Name")
                    mandatory: true
                    error: nameInput.effectiveError

                    TextInputField {
                        id: nameInput

                        Layout.fillWidth: true
                        error: staffModel.nameError
                        Accessible.name: nameFormField.label
                    }
                }

                FormField {
                    id: roleFormField

                    Layout.fillWidth: true
                    label: qsTr("Role")
                    helpText: !editDialog.adding && section.ownRowSelected ? qsTr("If you change your own role, you will have to log in again.") : ""

                    SelectField {
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        model: staffModel.roleOptions
                        value: editDialog.roleKey
                        Accessible.name: roleFormField.label
                        onValueActivated: value => editDialog.roleKey = value
                    }
                }

                FormField {
                    id: newPinFormField

                    Layout.fillWidth: true
                    visible: editDialog.adding
                    label: qsTr("PIN")
                    mandatory: true
                    helpText: qsTr("4 to 8 digits. Tell it to this person, who can change it later.")
                    error: newPinInput.effectiveError

                    PinField {
                        id: newPinInput

                        Layout.fillWidth: true
                        error: staffModel.pinError
                        Accessible.name: newPinFormField.label
                    }
                }

                FormField {
                    id: newPinRepeatFormField

                    Layout.fillWidth: true
                    visible: editDialog.adding
                    label: qsTr("Repeat the PIN")
                    mandatory: true
                    error: newPinRepeatInput.effectiveError

                    PinField {
                        id: newPinRepeatInput

                        Layout.fillWidth: true
                        error: staffModel.pinRepeatError
                        Accessible.name: newPinRepeatFormField.label
                    }
                }
            }

            AppDialog {
                id: resetPinDialog

                property double targetId: 0
                property string targetName

                function openFor(id, name) {
                    staffModel.clearErrors();
                    resetPinDialog.targetId = id;
                    resetPinDialog.targetName = name;
                    resetPinInput.clear();
                    resetPinRepeatInput.clear();
                    resetPinDialog.open();
                    resetPinInput.forceActiveFocus(Qt.TabFocusReason);
                }

                size: AppDialog.Small
                title: qsTr("Reset the PIN of %1").arg(targetName)
                primaryText: qsTr("Reset PIN")
                onPrimaryClicked: {
                    if (staffModel.resetPin(resetPinDialog.targetId, resetPinInput.text, resetPinRepeatInput.text)) {
                        resetPinDialog.close();
                        Toasts.info(qsTr("The PIN of %1 was reset.").arg(resetPinDialog.targetName));
                    }
                }

                FormField {
                    id: resetPinFormField

                    Layout.fillWidth: true
                    label: qsTr("New PIN")
                    mandatory: true
                    helpText: qsTr("4 to 8 digits. This also ends a lockout.")
                    error: resetPinInput.effectiveError

                    PinField {
                        id: resetPinInput

                        Layout.fillWidth: true
                        error: staffModel.pinError
                        Accessible.name: resetPinFormField.label
                    }
                }

                FormField {
                    id: resetPinRepeatFormField

                    Layout.fillWidth: true
                    label: qsTr("Repeat the PIN")
                    mandatory: true
                    error: resetPinRepeatInput.effectiveError

                    PinField {
                        id: resetPinRepeatInput

                        Layout.fillWidth: true
                        error: staffModel.pinRepeatError
                        Accessible.name: resetPinRepeatFormField.label
                    }
                }
            }
        }
    }
}
