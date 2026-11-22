pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The team, the match rules, the load signals (P19) and the Staff section, for those who may manage
// settings and staff; the modules preference of this computer and the custom metrics (P17) for
// every role (P15 decision 4). Other settings arrive with later packets.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})
    readonly property bool canManageStaff: AppState.can("ManageStaff")
    readonly property bool canManageSettings: AppState.can("ManageSettings")
    // Exposed for tests: null without ManageStaff.
    readonly property Item staffSection: staffLoader.item as Item
    // Exposed for tests.
    readonly property alias teamDialog: teamDialog
    readonly property alias loadDialog: loadDialog
    // Exposed for tests: the metrics card, in its tab or alone.
    readonly property Item metricsSection: (root.canManageStaff ? metricsTabLoader.item : metricsLoader.item) as Item

    signal navigateRequested(string key, var args)

    TeamSettingsViewModel {
        id: teamSettings

        onErrorOccurred: message => Toasts.error(message)
    }

    LoadSettingsViewModel {
        id: loadSettings

        onErrorOccurred: message => Toasts.error(message)
    }

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

        // Two short cards side by side, so Staff and the metrics below keep their rows at the minimum
        // window (P17).
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.l

            // P19 decision 2: the name and the season of the team; edited in a dialog, so the card
            // stays as short as its neighbours.
            Card {
                objectName: "teamCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                visible: root.canManageSettings
                title: qsTr("Team")

                actions: SecondaryButton {
                    objectName: "editTeamButton"
                    visible: teamSettings.canEdit
                    text: qsTr("Edit")
                    onClicked: teamDialog.openForEditing()
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacing.m
                    rowSpacing: Theme.spacing.s

                    Label {
                        text: qsTr("Team name")
                        color: Theme.textMuted
                    }

                    Label {
                        objectName: "teamNameValue"
                        Layout.fillWidth: true
                        text: teamSettings.teamName
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.text
                    }

                    Label {
                        text: qsTr("Season")
                        color: Theme.textMuted
                    }

                    Label {
                        objectName: "teamSeasonValue"
                        Layout.fillWidth: true
                        text: teamSettings.season
                        textFormat: Text.PlainText
                        color: Theme.text
                    }
                }
            }

            // P12 decision 3: one threshold for every competition, kept on this computer.
            Card {
                objectName: "matchRulesCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
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

            // P19: the thresholds of the load signals (SPEC section 7.1), kept on this computer.
            Card {
                objectName: "loadThresholdsCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                visible: root.canManageSettings
                title: qsTr("Load signals")

                actions: SecondaryButton {
                    objectName: "editLoadSignalsButton"
                    visible: loadSettings.canEdit
                    text: qsTr("Edit")
                    onClicked: loadDialog.openForEditing()
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacing.m
                    rowSpacing: Theme.spacing.s

                    Repeater {
                        model: [qsTr("High ratio"), qsTr("Elevated ratio"), qsTr("Underload ratio"), qsTr("Monotony")]

                        delegate: Label {
                            required property string modelData
                            required property int index

                            Layout.row: index
                            Layout.column: 0
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                    }

                    Repeater {
                        model: [qsTr("> %1").arg(loadSettings.ratioHigh), qsTr("> %1").arg(loadSettings.ratioElevated), qsTr("< %1").arg(loadSettings.ratioLow), qsTr("> %1").arg(loadSettings.monotonyHigh)]

                        delegate: Label {
                            required property string modelData
                            required property int index

                            objectName: "loadSignalValue" + index
                            Layout.row: index
                            Layout.column: 1
                            text: modelData
                            color: Theme.text
                        }
                    }
                }
            }

            // P15 decision 4: a preference of this computer, like the theme; every role may change it.
            Card {
                objectName: "modulesCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
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
        }

        // P17: Staff and the custom metrics share the room below, each at the full width; a role
        // without ManageStaff sees only the metrics, which every role reads.
        AppTabs {
            id: settingsTabs

            objectName: "settingsTabs"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.canManageStaff
            titles: [qsTr("Staff"), qsTr("Custom metrics")]

            Loader {
                id: staffLoader

                active: root.canManageStaff
                sourceComponent: staffComponent
            }

            Loader {
                id: metricsTabLoader

                active: root.canManageStaff
                sourceComponent: metricsComponent
            }
        }

        Loader {
            id: metricsLoader

            Layout.fillWidth: true
            Layout.fillHeight: true
            active: !root.canManageStaff
            visible: active
            sourceComponent: metricsComponent
        }
    }

    AppDialog {
        id: teamDialog

        function openForEditing() {
            teamSettings.reload();
            teamSettings.clearErrors();
            teamNameInput.text = teamSettings.teamName;
            teamSeasonInput.text = teamSettings.season;
            teamDialog.open();
            teamNameInput.forceActiveFocus(Qt.TabFocusReason);
        }

        objectName: "teamDialog"
        size: AppDialog.Small
        title: qsTr("Edit team")
        primaryText: qsTr("Save")
        onPrimaryClicked: {
            if (teamSettings.save(teamNameInput.text, teamSeasonInput.text)) {
                teamDialog.close();
                AppState.refresh();
                Toasts.info(qsTr("The team was saved."));
            } else if (teamSettings.nameError.length > 0) {
                teamNameInput.forceActiveFocus(Qt.TabFocusReason);
            } else if (teamSettings.seasonError.length > 0) {
                teamSeasonInput.forceActiveFocus(Qt.TabFocusReason);
            }
        }

        FormField {
            id: teamNameFormField

            Layout.fillWidth: true
            label: qsTr("Team name")
            mandatory: true
            error: teamNameInput.effectiveError

            TextInputField {
                id: teamNameInput

                objectName: "teamNameInput"
                Layout.fillWidth: true
                error: teamSettings.nameError
                Accessible.name: teamNameFormField.label
            }
        }

        FormField {
            id: teamSeasonFormField

            Layout.fillWidth: true
            label: qsTr("Season")
            mandatory: true
            helpText: qsTr("Two consecutive years, for example 2026/27.")
            error: teamSeasonInput.effectiveError

            TextInputField {
                id: teamSeasonInput

                objectName: "teamSeasonInput"
                Layout.fillWidth: true
                error: teamSettings.seasonError
                Accessible.name: teamSeasonFormField.label
            }
        }
    }

    AppDialog {
        id: loadDialog

        function openForEditing() {
            loadSettings.clearErrors();
            ratioHighInput.text = loadSettings.ratioHigh;
            ratioElevatedInput.text = loadSettings.ratioElevated;
            ratioLowInput.text = loadSettings.ratioLow;
            monotonyHighInput.text = loadSettings.monotonyHigh;
            loadDialog.open();
            ratioHighInput.forceActiveFocus(Qt.TabFocusReason);
        }

        function focusFirstError() {
            if (loadSettings.ratioHighError.length > 0)
                ratioHighInput.forceActiveFocus(Qt.TabFocusReason);
            else if (loadSettings.ratioElevatedError.length > 0)
                ratioElevatedInput.forceActiveFocus(Qt.TabFocusReason);
            else if (loadSettings.ratioLowError.length > 0)
                ratioLowInput.forceActiveFocus(Qt.TabFocusReason);
            else if (loadSettings.monotonyHighError.length > 0)
                monotonyHighInput.forceActiveFocus(Qt.TabFocusReason);
        }

        objectName: "loadDialog"
        size: AppDialog.Small
        title: qsTr("Edit load signals")
        primaryText: qsTr("Save")
        onPrimaryClicked: {
            if (loadSettings.save(ratioHighInput.text, ratioElevatedInput.text, ratioLowInput.text, monotonyHighInput.text)) {
                loadDialog.close();
                Toasts.info(qsTr("The load signals were saved."));
            } else {
                loadDialog.focusFirstError();
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Ratio = acute load (7 days) / chronic load (28 days). A signal is a reason to talk to the player, not an injury prediction.")
            textFormat: Text.PlainText
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        FormField {
            id: ratioHighFormField

            Layout.fillWidth: true
            label: qsTr("High: ratio above")
            mandatory: true
            error: ratioHighInput.effectiveError

            TextInputField {
                id: ratioHighInput

                objectName: "ratioHighInput"
                Layout.fillWidth: true
                error: loadSettings.ratioHighError
                Accessible.name: ratioHighFormField.label
            }
        }

        FormField {
            id: ratioElevatedFormField

            Layout.fillWidth: true
            label: qsTr("Elevated: ratio above")
            mandatory: true
            error: ratioElevatedInput.effectiveError

            TextInputField {
                id: ratioElevatedInput

                objectName: "ratioElevatedInput"
                Layout.fillWidth: true
                error: loadSettings.ratioElevatedError
                Accessible.name: ratioElevatedFormField.label
            }
        }

        FormField {
            id: ratioLowFormField

            Layout.fillWidth: true
            label: qsTr("Underloaded: ratio below")
            mandatory: true
            error: ratioLowInput.effectiveError

            TextInputField {
                id: ratioLowInput

                objectName: "ratioLowInput"
                Layout.fillWidth: true
                error: loadSettings.ratioLowError
                Accessible.name: ratioLowFormField.label
            }
        }

        FormField {
            id: monotonyHighFormField

            Layout.fillWidth: true
            label: qsTr("Elevated: monotony above")
            mandatory: true
            error: monotonyHighInput.effectiveError

            TextInputField {
                id: monotonyHighInput

                objectName: "monotonyHighInput"
                Layout.fillWidth: true
                error: loadSettings.monotonyHighError
                Accessible.name: monotonyHighFormField.label
            }
        }

        SecondaryButton {
            objectName: "restoreLoadDefaultsButton"
            enabled: !loadSettings.isDefault
            text: qsTr("Restore defaults")
            onClicked: {
                if (loadSettings.restoreDefaults()) {
                    loadDialog.close();
                    Toasts.info(qsTr("The default load signals are in use again."));
                }
            }
        }
    }

    Component {
        id: metricsComponent

        // P17: every role reads the custom metrics; ManageMetrics changes them.
        Card {
            id: metricsSection

            // Exposed for tests.
            readonly property MetricDefinitionsViewModel viewModel: metricDefinitions
            readonly property alias table: metricsTable
            readonly property alias dialog: metricDialog
            property var selectedMetric: ({})
            readonly property bool hasSelection: selectedMetric.metricId !== undefined
            readonly property bool selectionHasValues: hasSelection && selectedMetric.valueCount > 0

            function syncSelection() {
                metricsSection.selectedMetric = metricDefinitions.definitions.rowAt(metricsTable.currentIndex);
            }

            // All views stay loaded, and the Metrics section may have added or cleared values since:
            // the locks of Delete, type and scope come from them.
            Connections {
                target: root

                function onVisibleChanged() {
                    if (root.visible)
                        metricsSection.reload();
                }
            }

            // Reads the list again; the same metric stays selected.
            function reload() {
                const id = metricsSection.selectedMetric.metricId ?? 0;
                metricDefinitions.reload();
                metricsTable.currentIndex = metricDefinitions.definitions.rowOf(id);
                metricsSection.syncSelection();
            }

            function editSelected() {
                if (metricsSection.hasSelection && metricDefinitions.canManage)
                    metricDialog.openForEditing(metricsSection.selectedMetric);
            }

            function deleteSelected() {
                if (metricsSection.hasSelection)
                    metricDefinitions.remove(metricsSection.selectedMetric.metricId);
            }

            objectName: "metricsCard"
            title: qsTr("Custom metrics")
            Component.onCompleted: metricsSection.syncSelection()

            actions: PrimaryButton {
                objectName: "addMetricButton"
                visible: metricDefinitions.canManage
                text: qsTr("Add metric")
                onClicked: metricDialog.openForAdding("")
            }

            MetricDefinitionsViewModel {
                id: metricDefinitions

                onErrorOccurred: message => Toasts.error(message)
                onRemoved: name => Toasts.show(qsTr("Metric %1 was deleted.").arg(name), qsTr("Undo"), () => metricDefinitions.undoRemove())
                onDefinitionsChanged: {
                    metricsTable.currentIndex = metricDefinitions.definitions.rowOf(metricsSection.selectedMetric.metricId ?? 0);
                    metricsSection.syncSelection();
                }
            }

            DataTable {
                id: metricsTable

                objectName: "metricsTable"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Accessible.name: qsTr("Custom metrics")
                model: metricDefinitions.definitions
                columns: [
                    {
                        "role": "name",
                        "title": qsTr("Name", "custom metric"),
                        "width": 0.34,
                        "sortable": false
                    },
                    {
                        "role": "valueTypeText",
                        "title": qsTr("Type"),
                        "width": 0.22,
                        "sortable": false
                    },
                    {
                        "role": "scopeText",
                        "title": qsTr("Entered for"),
                        "width": 0.26,
                        "sortable": false
                    },
                    {
                        "role": "valueCount",
                        "title": qsTr("Values"),
                        "width": 0.18,
                        "align": Qt.AlignRight,
                        "sortable": false
                    }
                ]
                emptyState: metricsEmptyState
                onCurrentIndexChanged: metricsSection.syncSelection()
                onActivated: metricsSection.editSelected()
            }

            Component {
                id: metricsEmptyState

                EmptyState {
                    objectName: "noMetricsState"
                    iconName: "metrics"
                    title: metricDefinitions.loadError.length > 0 ? qsTr("The metrics could not be read") : qsTr("No custom metrics yet")
                    description: metricDefinitions.loadError.length > 0 ? metricDefinitions.loadError : metricDefinitions.canManage ? qsTr("Define what your club measures, such as a 30 m sprint or a jump, and enter it per session, match or day.") : qsTr("Ask the coach, the assistant or the analyst to define one.")
                    actionText: metricDefinitions.loadError.length > 0 ? qsTr("Try again") : metricDefinitions.canManage ? qsTr("Add metric") : ""
                    onActionTriggered: {
                        if (metricDefinitions.loadError.length > 0)
                            metricDefinitions.reload();
                        else
                            metricDialog.openForAdding("");
                    }
                }
            }

            Label {
                objectName: "metricDetails"
                Layout.fillWidth: true
                visible: metricsSection.hasSelection
                text: metricsSection.hasSelection ? qsTr("Over 28 days: %1 · %2").arg(metricsSection.selectedMetric.aggregationText).arg(metricsSection.selectedMetric.rangeText.length > 0 ? qsTr("Allowed: %1").arg(metricsSection.selectedMetric.rangeText) : qsTr("No range")) : ""
                textFormat: Text.PlainText
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                visible: metricDefinitions.canManage
                spacing: Theme.spacing.m

                SecondaryButton {
                    objectName: "editMetricButton"
                    enabled: metricsSection.hasSelection
                    text: qsTr("Edit")
                    onClicked: metricsSection.editSelected()
                }

                // A disabled button gets no hover, so the wrapper shows why it is disabled.
                Item {
                    implicitWidth: deleteMetricButton.implicitWidth
                    implicitHeight: deleteMetricButton.implicitHeight

                    HoverHandler {
                        id: deleteMetricHover
                    }

                    DangerButton {
                        id: deleteMetricButton

                        objectName: "deleteMetricButton"
                        anchors.fill: parent
                        enabled: metricsSection.hasSelection && !metricsSection.selectionHasValues
                        text: qsTr("Delete")
                        onClicked: metricsSection.deleteSelected()
                    }

                    ToolTip.visible: deleteMetricHover.hovered && metricsSection.selectionHasValues
                    ToolTip.text: qsTr("This metric already has values, so it cannot be deleted: the values would be lost.")
                    ToolTip.delay: Theme.tooltipDelay
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            Label {
                objectName: "metricDeleteHint"
                Layout.fillWidth: true
                visible: metricDefinitions.canManage && metricsSection.selectionHasValues
                text: qsTr("A metric with values cannot be deleted: its values would be lost.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize.caption
                wrapMode: Text.Wrap
            }

            MetricDefinitionDialog {
                id: metricDialog

                viewModel: metricDefinitions
            }
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
                    helpText: qsTr("4 to 8 digits. Tell it to this person, who sets their own PIN at the first login.")
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
                    helpText: section.ownRowSelected ? qsTr("4 to 8 digits. This also ends a lockout.") : qsTr("4 to 8 digits. This also ends a lockout. The person sets their own PIN at the next login.")
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
