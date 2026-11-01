pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Hidden UI kit gallery (Ctrl+Shift+K). Shows every shared component in normal, focused,
// disabled and error states, in both themes and languages. Sample data lives here only.
Item {
    id: root

    property alias currentTabIndex: galleryTabs.currentIndex
    readonly property int tabCount: galleryTabs.titles.length
    property int undoCount: 0
    property string lastActionText: qsTr("None")
    readonly property bool dialogOpen: sampleDialog.visible

    signal navigateRequested(string key)

    function triggerSampleUndoToast() {
        Toasts.show(qsTr("Player archived from squad."), qsTr("Undo"), function () {
            root.undoCount += 1;
            root.lastActionText = qsTr("Undo triggered (%1)").arg(root.undoCount);
        });
    }

    function openSampleDialog() {
        sampleDialog.open();
    }

    function closeSampleDialog() {
        sampleDialog.close();
    }

    function focusDefaultControl() {
        if (!root.visible)
            return;
        if (root.currentTabIndex === 1)
            normalTextField.forceActiveFocus(Qt.TabFocusReason);
        else if (root.currentTabIndex === 0)
            firstPrimaryButton.forceActiveFocus(Qt.TabFocusReason);
    }

    readonly property var samplePositions: [
        {
            "value": "GK",
            "text": qsTr("Goalkeeper (GK)")
        },
        {
            "value": "DF",
            "text": qsTr("Defender (DF)")
        },
        {
            "value": "MF",
            "text": qsTr("Midfielder (MF)")
        },
        {
            "value": "FW",
            "text": qsTr("Forward (FW)")
        }
    ]

    readonly property var sampleSegmentOptions: [
        {
            "value": "all",
            "text": qsTr("All")
        },
        {
            "value": "ok",
            "text": qsTr("Available")
        },
        {
            "value": "warn",
            "text": qsTr("Caution")
        },
        {
            "value": "danger",
            "text": qsTr("Unavailable")
        }
    ]

    readonly property var tableFilterOptions: [
        {
            "value": "all",
            "text": qsTr("All (30)")
        },
        {
            "value": "ok",
            "text": qsTr("Available")
        },
        {
            "value": "warn",
            "text": qsTr("Caution")
        },
        {
            "value": "danger",
            "text": qsTr("Injured")
        },
        {
            "value": "empty",
            "text": qsTr("Empty state")
        }
    ]

    // 30 sample squad rows for DataTable.
    readonly property var rawPlayers: [
        { "number": 1, "name": "Mateusz Kowalski", "position": "GK", "age": 28, "minutes": 810, "status": "ok" },
        { "number": 12, "name": "Łukasz Nowak", "position": "GK", "age": 21, "minutes": 180, "status": "ok" },
        { "number": 22, "name": "Kamil Wiśniewski", "position": "GK", "age": 19, "minutes": 0, "status": "info" },
        { "number": 2, "name": "Piotr Wójcik", "position": "DF", "age": 26, "minutes": 765, "status": "ok" },
        { "number": 3, "name": "Bartosz Kowalczyk", "position": "DF", "age": 24, "minutes": 690, "status": "warn" },
        { "number": 4, "name": "Tomasz Kamiński", "position": "DF", "age": 30, "minutes": 810, "status": "ok" },
        { "number": 5, "name": "Michał Lewandowski", "position": "DF", "age": 27, "minutes": 540, "status": "danger" },
        { "number": 14, "name": "Jakub Zieliński", "position": "DF", "age": 22, "minutes": 420, "status": "ok" },
        { "number": 15, "name": "Szymon Szymański", "position": "DF", "age": 25, "minutes": 610, "status": "ok" },
        { "number": 16, "name": "Dawid Woźniak", "position": "DF", "age": 20, "minutes": 230, "status": "info" },
        { "number": 24, "name": "Marek Dąbrowski", "position": "DF", "age": 29, "minutes": 495, "status": "warn" },
        { "number": 25, "name": "Paweł Kozłowski", "position": "DF", "age": 23, "minutes": 310, "status": "ok" },
        { "number": 6, "name": "Krzysztof Jankowski", "position": "MF", "age": 27, "minutes": 780, "status": "ok" },
        { "number": 8, "name": "Wojciech Mazur", "position": "MF", "age": 25, "minutes": 720, "status": "ok" },
        { "number": 10, "name": "Sebastian Kwiatkowski", "position": "MF", "age": 24, "minutes": 685, "status": "warn" },
        { "number": 13, "name": "Adrian Krawczyk", "position": "MF", "age": 21, "minutes": 390, "status": "ok" },
        { "number": 17, "name": "Marcin Piotrowski", "position": "MF", "age": 28, "minutes": 510, "status": "ok" },
        { "number": 18, "name": "Grzegorz Grabowski", "position": "MF", "age": 23, "minutes": 460, "status": "danger" },
        { "number": 19, "name": "Rafał Nowakowski", "position": "MF", "age": 20, "minutes": 195, "status": "ok" },
        { "number": 20, "name": "Damian Pawłowski", "position": "MF", "age": 26, "minutes": 640, "status": "ok" },
        { "number": 21, "name": "Patryk Michalski", "position": "MF", "age": 22, "minutes": 275, "status": "info" },
        { "number": 23, "name": "Filip Król", "position": "MF", "age": 19, "minutes": 120, "status": "ok" },
        { "number": 7, "name": "Maciej Wieczorek", "position": "FW", "age": 25, "minutes": 740, "status": "ok" },
        { "number": 9, "name": "Robert Jabłoński", "position": "FW", "age": 29, "minutes": 795, "status": "ok" },
        { "number": 11, "name": "Kacper Wróbel", "position": "FW", "age": 22, "minutes": 580, "status": "warn" },
        { "number": 26, "name": "Karol Majewski", "position": "FW", "age": 24, "minutes": 340, "status": "ok" },
        { "number": 27, "name": "Dominik Olszewski", "position": "FW", "age": 20, "minutes": 150, "status": "danger" },
        { "number": 28, "name": "Artur Stępień", "position": "FW", "age": 23, "minutes": 290, "status": "ok" },
        { "number": 29, "name": "Igor Malinowski", "position": "FW", "age": 18, "minutes": 65, "status": "info" },
        { "number": 30, "name": "Hubert Górski", "position": "FW", "age": 21, "minutes": 210, "status": "ok" }
    ]

    readonly property var filteredPlayers: {
        const filter = tableFilter.currentValue;
        if (filter === "empty")
            return [];
        const query = tableSearch.text.trim().toLowerCase();
        const role = squadTable.sortRole;
        const asc = squadTable.sortAscending;
        const rows = [];
        for (let i = 0; i < root.rawPlayers.length; ++i) {
            const p = root.rawPlayers[i];
            if (filter !== "all" && p.status !== filter)
                continue;
            if (query.length > 0 && !p.name.toLowerCase().includes(query) && !String(p.number).includes(query))
                continue;
            rows.push(p);
        }
        rows.sort(function (a, b) {
            const va = a[role];
            const vb = b[role];
            if (va === vb)
                return 0;
            const cmp = typeof va === "string" ? va.localeCompare(vb) : (va < vb ? -1 : 1);
            return asc ? cmp : -cmp;
        });
        return rows;
    }

    onVisibleChanged: focusDefaultControl()
    onCurrentTabIndexChanged: focusDefaultControl()
    Component.onCompleted: focusDefaultControl()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            PageHeader {
                Layout.fillWidth: true
                title: qsTr("Component gallery")
            }

            SegmentedControl {
                Accessible.name: qsTr("Gallery language")
                model: [
                    { "value": "pl", "text": "PL" },
                    { "value": "en", "text": "EN" }
                ]
                currentValue: LanguageManager.language
                onActivated: value => LanguageManager.setLanguage(String(value))
            }

            SecondaryButton {
                text: AppSettings.darkTheme ? qsTr("Light theme") : qsTr("Dark theme")
                onClicked: AppSettings.darkTheme = !AppSettings.darkTheme
            }
        }

        AppTabs {
            id: galleryTabs

            Layout.fillWidth: true
            Layout.fillHeight: true
            titles: [
                qsTr("Buttons & badges"),
                qsTr("Form fields"),
                qsTr("Data table"),
                qsTr("Containers & feedback")
            ]

            // Tab 0: Buttons, SegmentedControl, StatusBadge
            Flickable {
                contentWidth: width
                contentHeight: buttonsLayout.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: buttonsLayout

                    width: parent.width
                    spacing: Theme.spacing.l

                    Card {
                        Layout.fillWidth: true
                        title: qsTr("Buttons (normal, touch, disabled)")

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 5
                            rowSpacing: Theme.spacing.m
                            columnSpacing: Theme.spacing.m

                            Label {
                                text: qsTr("Normal")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.body
                            }

                            PrimaryButton {
                                id: firstPrimaryButton

                                text: qsTr("Primary action")
                                onClicked: root.lastActionText = qsTr("Primary clicked")
                            }

                            SecondaryButton {
                                text: qsTr("Secondary action")
                                onClicked: root.lastActionText = qsTr("Secondary clicked")
                            }

                            DangerButton {
                                text: qsTr("Delete item")
                                onClicked: root.lastActionText = qsTr("Danger clicked")
                            }

                            Row {
                                spacing: Theme.spacing.s

                                IconButton {
                                    iconName: "edit"
                                    text: qsTr("Edit item")
                                }

                                IconButton {
                                    iconName: "trash"
                                    iconColor: Theme.danger
                                    text: qsTr("Remove item")
                                }

                                IconButton {
                                    iconName: "undo"
                                    text: qsTr("Undo last change")
                                }
                            }

                            Label {
                                text: qsTr("Touch (44 px)")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.body
                            }

                            PrimaryButton {
                                touch: true
                                text: qsTr("Confirm sub")
                            }

                            SecondaryButton {
                                touch: true
                                text: qsTr("Cancel")
                            }

                            DangerButton {
                                touch: true
                                text: qsTr("End match")
                            }

                            IconButton {
                                touch: true
                                iconName: "plus"
                                text: qsTr("Add event")
                            }

                            Label {
                                text: qsTr("Disabled")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.body
                            }

                            PrimaryButton {
                                enabled: false
                                text: qsTr("Primary action")
                            }

                            SecondaryButton {
                                enabled: false
                                text: qsTr("Secondary action")
                            }

                            DangerButton {
                                enabled: false
                                text: qsTr("Delete item")
                            }

                            IconButton {
                                enabled: false
                                iconName: "edit"
                                text: qsTr("Edit item")
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.l

                        Card {
                            Layout.fillWidth: true
                            title: qsTr("SegmentedControl (←/→ keys)")

                            FormField {
                                label: qsTr("Squad filter (normal)")

                                SegmentedControl {
                                    Accessible.name: qsTr("Squad filter")
                                    model: root.sampleSegmentOptions
                                    currentValue: "all"
                                }
                            }

                            FormField {
                                label: qsTr("Attendance status (touch)")

                                SegmentedControl {
                                    touch: true
                                    Accessible.name: qsTr("Attendance status")
                                    model: [
                                        { "value": "present", "text": qsTr("Present") },
                                        { "value": "modified", "text": qsTr("Modified") },
                                        { "value": "absent", "text": qsTr("Absent") }
                                    ]
                                    currentValue: "present"
                                }
                            }

                            FormField {
                                label: qsTr("Disabled segmented control")

                                SegmentedControl {
                                    enabled: false
                                    Accessible.name: qsTr("Disabled filter")
                                    model: root.sampleSegmentOptions
                                    currentValue: "ok"
                                }
                            }
                        }

                        Card {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            title: qsTr("StatusBadge (icon + color + word)")

                            Row {
                                spacing: Theme.spacing.m

                                StatusBadge {
                                    kind: StatusBadge.Ok
                                    text: qsTr("Available")
                                }

                                StatusBadge {
                                    kind: StatusBadge.Warn
                                    text: qsTr("Talk to player")
                                }

                                StatusBadge {
                                    kind: StatusBadge.Danger
                                    text: qsTr("Injured")
                                }

                                StatusBadge {
                                    kind: StatusBadge.Info
                                    text: qsTr("Rest day")
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Last gallery interaction: %1").arg(root.lastActionText)
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.caption
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }
                    }
                }
            }

            // Tab 1: Form fields in normal, focused, error and disabled states
            Flickable {
                contentWidth: width
                contentHeight: fieldsGrid.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                GridLayout {
                    id: fieldsGrid

                    width: parent.width
                    columns: 3
                    rowSpacing: Theme.spacing.l
                    columnSpacing: Theme.spacing.l

                    Card {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        title: qsTr("Normal & focused (Tab)")

                        FormField {
                            id: normalNameField

                            Layout.fillWidth: true
                            label: qsTr("Player surname")
                            mandatory: true
                            helpText: qsTr("1–50 characters after trimming.")
                            error: normalTextField.effectiveError

                            TextInputField {
                                id: normalTextField

                                Layout.fillWidth: true
                                text: "Kowalski"
                                maximumLength: 50
                                Accessible.name: normalNameField.label
                            }
                        }

                        FormField {
                            id: normalShirtField

                            Layout.fillWidth: true
                            label: qsTr("Shirt number")
                            helpText: qsTr("Optional, 1–99. Use ↑/↓ or buttons.")
                            error: normalNumberField.effectiveError

                            NumberField {
                                id: normalNumberField

                                from: 1
                                to: 99
                                optional: true
                                value: 9
                                Accessible.name: normalShirtField.label
                            }
                        }

                        FormField {
                            id: normalDateFormField

                            Layout.fillWidth: true
                            label: qsTr("Session date (%1)").arg(normalDateField.displayFormat)
                            mandatory: true
                            helpText: qsTr("Alt+↓ or the calendar button opens the popup.")
                            error: normalDateField.effectiveError

                            DateField {
                                id: normalDateField

                                Layout.fillWidth: true
                                date: "2026-11-01"
                                minimumDate: "2026-01-01"
                                maximumDate: "2027-12-31"
                                Accessible.name: normalDateFormField.label
                            }
                        }

                        FormField {
                            id: normalPosField

                            Layout.fillWidth: true
                            label: qsTr("Primary position")
                            mandatory: true
                            error: normalSelectField.effectiveError

                            SelectField {
                                id: normalSelectField

                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "value"
                                model: root.samplePositions
                                value: "MF"
                                Accessible.name: normalPosField.label
                            }
                        }

                        FormField {
                            id: normalSearchFormField

                            Layout.fillWidth: true
                            label: qsTr("Debounced search (200 ms)")
                            helpText: qsTr("Committed query: \"%1\"").arg(sampleSearchField.text)

                            SearchField {
                                id: sampleSearchField

                                Layout.fillWidth: true
                                placeholderText: qsTr("Search player or number…")
                                Accessible.name: normalSearchFormField.label
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        title: qsTr("Error state")

                        FormField {
                            id: errorNameField

                            Layout.fillWidth: true
                            label: qsTr("Player surname")
                            mandatory: true
                            error: errorTextField.effectiveError

                            TextInputField {
                                id: errorTextField

                                Layout.fillWidth: true
                                placeholderText: qsTr("Enter surname")
                                error: qsTr("Player surname is required.")
                                Accessible.name: errorNameField.label
                            }
                        }

                        FormField {
                            id: errorRpeField

                            Layout.fillWidth: true
                            label: qsTr("Session RPE (0–10)")
                            mandatory: true
                            error: errorNumberField.effectiveError

                            NumberField {
                                id: errorNumberField

                                from: 0
                                to: 10
                                value: 8
                                error: qsTr("Enter a number from 0 to 10.")
                                Accessible.name: errorRpeField.label
                            }
                        }

                        FormField {
                            id: errorDateFormField

                            Layout.fillWidth: true
                            label: qsTr("Match date")
                            mandatory: true
                            error: errorDateField.effectiveError

                            DateField {
                                id: errorDateField

                                Layout.fillWidth: true
                                date: "2026-11-01"
                                error: qsTr("Another match is already scheduled on this date.")
                                Accessible.name: errorDateFormField.label
                            }
                        }

                        FormField {
                            id: errorPosField

                            Layout.fillWidth: true
                            label: qsTr("Primary position")
                            mandatory: true
                            error: errorSelectField.effectiveError

                            SelectField {
                                id: errorSelectField

                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "value"
                                model: root.samplePositions
                                placeholderText: qsTr("Select position…")
                                error: qsTr("Choose a primary position.")
                                Accessible.name: errorPosField.label
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        title: qsTr("Disabled state")

                        FormField {
                            id: disabledNameField

                            Layout.fillWidth: true
                            label: qsTr("Player surname")

                            TextInputField {
                                Layout.fillWidth: true
                                enabled: false
                                text: "Nowak"
                                Accessible.name: disabledNameField.label
                            }
                        }

                        FormField {
                            id: disabledNumberFormField

                            Layout.fillWidth: true
                            label: qsTr("Shirt number")

                            NumberField {
                                enabled: false
                                value: 12
                                Accessible.name: disabledNumberFormField.label
                            }
                        }

                        FormField {
                            id: disabledDateFormField

                            Layout.fillWidth: true
                            label: qsTr("Session date")

                            DateField {
                                Layout.fillWidth: true
                                enabled: false
                                date: "2026-11-01"
                                Accessible.name: disabledDateFormField.label
                            }
                        }

                        FormField {
                            id: disabledSelectFormField

                            Layout.fillWidth: true
                            label: qsTr("Primary position")

                            SelectField {
                                Layout.fillWidth: true
                                enabled: false
                                textRole: "text"
                                valueRole: "value"
                                model: root.samplePositions
                                value: "GK"
                                Accessible.name: disabledSelectFormField.label
                            }
                        }

                        FormField {
                            id: disabledSearchFormField

                            Layout.fillWidth: true
                            label: qsTr("Search")

                            SearchField {
                                Layout.fillWidth: true
                                enabled: false
                                Accessible.name: disabledSearchFormField.label
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }
            }

            // Tab 2: DataTable with 30 rows and a StatusBadge cell
            ColumnLayout {
                spacing: Theme.spacing.m

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.m

                    SearchField {
                        id: tableSearch

                        placeholderText: qsTr("Filter by name or shirt number…")
                        Accessible.name: qsTr("Filter sample squad")
                    }

                    SegmentedControl {
                        id: tableFilter

                        Accessible.name: qsTr("Availability filter")
                        model: root.tableFilterOptions
                        currentValue: "all"
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Label {
                        text: qsTr("Rows: %1").arg(squadTable.count)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.body
                    }
                }

                Component {
                    id: statusCellDelegate

                    DataTableCell {
                        id: statusCell

                        StatusBadge {
                            anchors.verticalCenter: parent.verticalCenter
                            kind: {
                                if (statusCell.value === "ok")
                                    return StatusBadge.Ok;
                                if (statusCell.value === "warn")
                                    return StatusBadge.Warn;
                                if (statusCell.value === "danger")
                                    return StatusBadge.Danger;
                                return StatusBadge.Info;
                            }
                            text: {
                                if (statusCell.value === "ok")
                                    return qsTr("Available");
                                if (statusCell.value === "warn")
                                    return qsTr("Caution");
                                if (statusCell.value === "danger")
                                    return qsTr("Injured");
                                return qsTr("Rest");
                            }
                        }
                    }
                }

                Component {
                    id: tableEmptyComponent

                    EmptyState {
                        iconName: "squad"
                        title: qsTr("No matching players")
                        description: qsTr("Clear the search filter or switch back to All to see the 30 sample rows.")
                        actionText: qsTr("Show all rows")
                        onActionTriggered: {
                            tableSearch.clear();
                            tableFilter.currentValue = "all";
                        }
                    }
                }

                DataTable {
                    id: squadTable

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Accessible.name: qsTr("Sample squad table")
                    model: root.filteredPlayers
                    sortRole: "number"
                    sortAscending: true
                    currentIndex: 0
                    emptyState: tableEmptyComponent
                    columns: [
                        {
                            "role": "number",
                            "title": qsTr("#"),
                            "width": 2 * Theme.controlHeight,
                            "align": Text.AlignRight
                        },
                        {
                            "role": "name",
                            "title": qsTr("Player"),
                            "width": 0.38
                        },
                        {
                            "role": "position",
                            "title": qsTr("Pos"),
                            "width": 0.12
                        },
                        {
                            "role": "age",
                            "title": qsTr("Age"),
                            "width": 0.12,
                            "align": Text.AlignRight
                        },
                        {
                            "role": "minutes",
                            "title": qsTr("Minutes"),
                            "width": 0.16,
                            "align": Text.AlignRight
                        },
                        {
                            "role": "status",
                            "title": qsTr("Status"),
                            "width": 0.22,
                            "delegate": statusCellDelegate
                        }
                    ]
                    onActivated: index => {
                        const row = root.filteredPlayers[index];
                        if (row)
                            Toasts.info(qsTr("Activated row: %1 (#%2)").arg(row.name).arg(row.number));
                    }
                }
            }

            // Tab 3: Card, EmptyState, AppDialog and Toasts
            GridLayout {
                columns: 2
                rowSpacing: Theme.spacing.l
                columnSpacing: Theme.spacing.l

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: qsTr("Card with header actions")
                    actions: [
                        IconButton {
                            iconName: "edit"
                            text: qsTr("Edit card")
                            onClicked: root.openSampleDialog()
                        },
                        IconButton {
                            iconName: "trash"
                            iconColor: Theme.danger
                            text: qsTr("Archive item")
                            onClicked: root.triggerSampleUndoToast()
                        }
                    ]

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Cards group related fields, tables or summaries on a surface container.")
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        spacing: Theme.spacing.m

                        PrimaryButton {
                            text: qsTr("Open sample dialog")
                            onClicked: root.openSampleDialog()
                        }

                        SecondaryButton {
                            text: qsTr("Show undo toast (10 s)")
                            onClicked: root.triggerSampleUndoToast()
                        }

                        SecondaryButton {
                            text: qsTr("Show info toast")
                            onClicked: Toasts.info(qsTr("Training session saved."))
                        }

                        DangerButton {
                            text: qsTr("Show error toast")
                            onClicked: Toasts.error(qsTr("Could not read row 14: expected a whole number."))
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Undo callback count: %1 • Last action: %2").arg(root.undoCount).arg(root.lastActionText)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: qsTr("EmptyState inside a Card")

                    EmptyState {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        iconName: "calendar"
                        title: qsTr("No sessions scheduled")
                        description: qsTr("Empty states always explain what is missing and offer the next step.")
                        actionText: qsTr("Add training session")
                        onActionTriggered: root.openSampleDialog()
                    }
                }
            }
        }
    }

    AppDialog {
        id: sampleDialog

        title: qsTr("Add player")
        primaryText: qsTr("Save player")
        secondaryText: qsTr("Cancel")
        dangerText: qsTr("Archive")
        onPrimaryClicked: {
            root.lastActionText = qsTr("Dialog saved");
            sampleDialog.close();
            Toasts.info(qsTr("Player saved."));
        }
        onDangerClicked: {
            sampleDialog.close();
            root.triggerSampleUndoToast();
        }

        FormField {
            id: dialogNameFormField

            Layout.fillWidth: true
            label: qsTr("Full name")
            mandatory: true
            error: dialogNameInput.effectiveError

            TextInputField {
                id: dialogNameInput

                Layout.fillWidth: true
                text: "Jan Kowalski"
                Accessible.name: dialogNameFormField.label
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            FormField {
                id: dialogNumberFormField

                Layout.fillWidth: true
                label: qsTr("Shirt number")
                error: dialogNumberInput.effectiveError

                NumberField {
                    id: dialogNumberInput

                    from: 1
                    to: 99
                    optional: true
                    value: 10
                    Accessible.name: dialogNumberFormField.label
                }
            }

            FormField {
                id: dialogPositionFormField

                Layout.fillWidth: true
                label: qsTr("Position")
                mandatory: true
                error: dialogPositionSelect.effectiveError

                SelectField {
                    id: dialogPositionSelect

                    Layout.fillWidth: true
                    textRole: "text"
                    valueRole: "value"
                    model: root.samplePositions
                    value: "MF"
                    Accessible.name: dialogPositionFormField.label
                }
            }
        }
    }
}
