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
    property string tableFilterValue: "all"
    property string tableSortRole: "number"
    property bool tableSortAscending: true
    readonly property bool dialogOpen: sampleDialog.visible

    signal navigateRequested(string key, var args)

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

    // Four weeks of one player for LoadChart, as constant sample values (no load arithmetic in QML):
    // three sessions and a match a week, a spike in the last week, and on day 14 a session and a match
    // of the same load (the tooltip tells them apart by series, not by value).
    readonly property var sampleLoad: ({
            "labels": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28"],
            "sessions": [0, 300, 0, 450, 240, 0, 0, 0, 300, 0, 450, 240, 0, 360, 0, 300, 0, 450, 240, 0, 0, 0, 600, 0, 900, 480, 0, 0],
            "matches": [0, 0, 0, 0, 0, 0, 540, 0, 0, 0, 0, 0, 0, 360, 0, 0, 0, 0, 0, 0, 540, 0, 0, 0, 0, 0, 0, 810],
            "acute": [0, 75, 56, 155, 176, 132, 234, 176, 207, 155, 229, 232, 174, 310, 233, 250, 187, 253, 250, 187, 275, 207, 305, 229, 397, 417, 313, 437],
            "chronic": [0, 21, 19, 49, 62, 58, 91, 85, 100, 93, 117, 126, 117, 159, 148, 158, 147, 168, 173, 161, 187, 174, 204, 190, 239, 255, 238, 277],
            // From day 15 on.
            "ratio": [1.57, 1.58, 1.27, 1.5, 1.44, 1.16, 1.47, 1.18, 1.5, 1.21, 1.66, 1.63, 1.32, 1.58]
        })
    // The day with a session and a match of the same load.
    readonly property int sampleEqualDay: 13

    // Values by day as chart points, starting at the given day.
    function samplePoints(values, firstDay) {
        return values.map((value, index) => Qt.point(firstDay + index, value));
    }

    readonly property var sampleWeek: {
        const day = (date, dayName, dateText, items, today) => ({
                    "date": date,
                    "dayName": dayName,
                    "dateText": dateText,
                    "today": today === true,
                    "items": items
                });
        return [day("2026-11-02", qsTr("Monday"), "02.11.2026", []), day("2026-11-03", qsTr("Tuesday"), "03.11.2026", [
                        {
                            "kind": "session",
                            "itemId": 1,
                            "time": "18:00",
                            "kindText": qsTr("Session"),
                            "title": qsTr("Training"),
                            "details": qsTr("90 min"),
                            "statusText": ""
                        }
                    ], true), day("2026-11-04", qsTr("Wednesday"), "04.11.2026", []), day("2026-11-05", qsTr("Thursday"), "05.11.2026", []), day("2026-11-06", qsTr("Friday"), "06.11.2026", []), day("2026-11-07", qsTr("Saturday"), "07.11.2026", [
                        {
                            "kind": "match",
                            "itemId": 2,
                            "time": "15:00",
                            "kindText": qsTr("Match"),
                            "title": qsTr("Sample FC"),
                            "details": qsTr("Home · League"),
                            "statusText": qsTr("Planned")
                        }
                    ]), day("2026-11-08", qsTr("Sunday"), "08.11.2026", [])];
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
        const filter = root.tableFilterValue;
        if (filter === "empty")
            return [];
        const query = tableSearch.text.trim().toLowerCase();
        const role = root.tableSortRole;
        const asc = root.tableSortAscending;
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
                qsTr("Charts"),
                qsTr("Tactics board"),
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

                                IconButton {
                                    iconName: "swap"
                                    text: qsTr("Swap with another slot")
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

                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.spacing.m

                                Repeater {
                                    model: [
                                        {
                                            "key": "available",
                                            "text": qsTr("Available")
                                        },
                                        {
                                            "key": "injured",
                                            "text": qsTr("Injured")
                                        },
                                        {
                                            "key": "ill",
                                            "text": qsTr("Ill")
                                        },
                                        {
                                            "key": "suspended",
                                            "text": qsTr("Suspended")
                                        },
                                        {
                                            "key": "absent",
                                            "text": qsTr("Absent")
                                        },
                                        {
                                            "key": "archived",
                                            "text": qsTr("Archived")
                                        }
                                    ]

                                    delegate: PlayerStatusBadge {
                                        required property var modelData

                                        statusKey: modelData.key
                                        text: modelData.text
                                    }
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.spacing.m

                                MatchStatusBadge {
                                    statusKey: "planned"
                                    text: qsTr("Planned")
                                }

                                MatchStatusBadge {
                                    statusKey: "live"
                                    text: qsTr("Live")
                                }

                                MatchStatusBadge {
                                    statusKey: "finished"
                                    text: qsTr("Finished")
                                }

                                SecondaryButton {
                                    id: plainTipSample

                                    text: qsTr("Hover: plain tooltip")

                                    PlainToolTip {
                                        visible: plainTipSample.hovered
                                        text: "<b>Sample</b> <a href='x'>Club</a>"
                                        delay: Theme.tooltipDelay
                                    }
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

                    // Import wizard (P16): steps, progress, column mapping and the report.
                    Card {
                        objectName: "galleryImport"
                        Layout.fillWidth: true
                        title: qsTr("Import: ImportStepIndicator, ProgressLine, ImportMappingTable, ImportReportTable")

                        ImportStepIndicator {
                            Layout.fillWidth: true
                            currentIndex: 3
                            steps: [qsTr("Type"), qsTr("File"), qsTr("Preview"), qsTr("Columns"), qsTr("Check"), qsTr("Done")]
                        }

                        ProgressLine {
                            Layout.fillWidth: true
                            value: 0.4
                            text: qsTr("Checking the rows… %1%").arg(40)
                        }

                        ProgressLine {
                            Layout.fillWidth: true
                            text: qsTr("Reading the file…")
                        }

                        ImportMappingTable {
                            Layout.fillWidth: true
                            model: sampleMapping
                            fieldOptions: [
                                {
                                    "value": "",
                                    "text": qsTr("Ignore column")
                                },
                                {
                                    "value": "last_name",
                                    "text": qsTr("Last name")
                                },
                                {
                                    "value": "first_name",
                                    "text": qsTr("First name")
                                },
                                {
                                    "value": "shirt_number",
                                    "text": qsTr("Shirt number")
                                }
                            ]
                            onFieldChosen: (column, field) => Toasts.info(qsTr("Column %1: %2").arg(column).arg(field))
                        }

                        ImportReportTable {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 4 * Theme.rowHeight + Theme.tableHeaderHeight
                            model: sampleReport
                        }

                        ListModel {
                            id: sampleMapping

                            ListElement { column: 0; header: "Nazwisko"; samples: "Kowalski, Nowak, Żółkiewski"; field: "last_name"; autoMatched: true }
                            ListElement { column: 1; header: "Imię"; samples: "Jan, Adam, Łukasz"; field: "first_name"; autoMatched: true }
                            ListElement { column: 2; header: "Telefon"; samples: "600 100 200"; field: ""; autoMatched: false }
                        }

                        ListModel {
                            id: sampleReport

                            ListElement { rowNumber: 2; statusKey: "ok"; statusText: qsTr("OK"); name: "Jan Kowalski"; action: qsTr("New player"); details: ""; message: "" }
                            ListElement { rowNumber: 3; statusKey: "warning"; statusText: qsTr("Warning"); name: "Adam Nowak"; action: qsTr("New player"); details: qsTr("position set by default — correct it in Squad"); message: qsTr("Row 3: position set by default — correct it in Squad.") }
                            ListElement { rowNumber: 14; statusKey: "error"; statusText: qsTr("Error"); name: "Piotr Lis"; action: qsTr("Not imported"); details: qsTr("shirt number 'abc', expected a number"); message: qsTr("Row 14: shirt number 'abc', expected a number.") }
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
                            id: normalTimeFormField

                            Layout.fillWidth: true
                            label: qsTr("Kick-off")
                            error: normalTimeField.effectiveError

                            TimeField {
                                id: normalTimeField

                                Layout.fillWidth: true
                                time: "18:30"
                                Accessible.name: normalTimeFormField.label
                            }
                        }

                        FormField {
                            id: normalSuggestionFormField

                            Layout.fillWidth: true
                            label: qsTr("Competition")
                            helpText: qsTr("Type, or press ↓ for the suggestions.")

                            SuggestionField {
                                Layout.fillWidth: true
                                suggestions: [qsTr("League"), qsTr("Cup"), qsTr("Friendly")]
                                Accessible.name: normalSuggestionFormField.label
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

                        FormField {
                            id: normalMultiSelectFormField

                            Layout.fillWidth: true
                            label: qsTr("Secondary positions")
                            helpText: qsTr("Space opens the list and toggles an item, Enter closes it.")

                            MultiSelectField {
                                Layout.fillWidth: true
                                model: root.samplePositions
                                values: ["MF"]
                                placeholderText: qsTr("None")
                                Accessible.name: normalMultiSelectFormField.label
                            }
                        }

                        FormField {
                            id: normalTextAreaFormField

                            Layout.fillWidth: true
                            label: qsTr("Notes")
                            helpText: qsTr("Shift+Enter: new line")

                            TextAreaField {
                                Layout.fillWidth: true
                                Accessible.name: normalTextAreaFormField.label
                            }
                        }

                        CheckBoxField {
                            text: qsTr("Show archived")
                            checked: true
                        }

                        FocusFlickable {
                            id: sampleFocusFlickable

                            Layout.fillWidth: true
                            implicitHeight: 2 * Theme.controlHeight
                            contentHeight: sampleFocusColumn.implicitHeight

                            Column {
                                id: sampleFocusColumn

                                width: sampleFocusFlickable.width
                                spacing: Theme.spacing.s

                                Repeater {
                                    model: 4

                                    delegate: TextInputField {
                                        required property int index

                                        width: sampleFocusColumn.width
                                        placeholderText: qsTr("Tab keeps field %1 in view").arg(index + 1)
                                    }
                                }
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

                    Card {
                        Layout.fillWidth: true
                        title: qsTr("PinField and PinPad")

                        FormField {
                            id: samplePinFormField

                            Layout.fillWidth: true
                            label: qsTr("PIN")
                            helpText: qsTr("Digits only, masked.")
                            error: samplePinField.effectiveError

                            PinField {
                                id: samplePinField

                                Layout.fillWidth: true
                                Accessible.name: samplePinFormField.label
                            }
                        }

                        PinPad {
                            onDigitPressed: digit => samplePinField.insert(samplePinField.length, digit)
                            onBackspacePressed: samplePinField.remove(samplePinField.length - 1, samplePinField.length)
                            onClearPressed: samplePinField.clear()
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        title: qsTr("LanguageThemeSwitch")

                        LanguageThemeSwitch {}
                    }

                    Card {
                        Layout.fillWidth: true
                        title: qsTr("AttendanceGrid (Tab, Enter, Esc, digits)")

                        ListModel {
                            id: sampleAttendance

                            ListElement {
                                shirtNumber: 7
                                playerName: qsTr("Jan Kowalski")
                                status: "present"
                                rpe: 6
                                minutes: 90
                                load: 540
                                cellsEnabled: true
                                rowError: ""
                                minutesHasError: false
                            }
                            ListElement {
                                shirtNumber: 9
                                playerName: qsTr("Adam Nowak")
                                status: "injured"
                                rpe: 0
                                minutes: 0
                                load: 0
                                cellsEnabled: false
                                rowError: ""
                                minutesHasError: false
                            }
                            ListElement {
                                shirtNumber: 10
                                playerName: qsTr("Piotr Zych")
                                status: "present"
                                rpe: 4
                                minutes: 300
                                load: 0
                                cellsEnabled: true
                                rowError: qsTr("Enter minutes from 0 to 240.")
                                minutesHasError: true
                            }
                        }

                        AttendanceGrid {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.tableHeaderHeight + 4 * Theme.rowHeight
                            Accessible.name: qsTr("Sample attendance")
                            model: sampleAttendance
                            statusOptions: [
                                { "value": "present", "text": qsTr("Present") },
                                { "value": "absent", "text": qsTr("Absent") },
                                { "value": "excused", "text": qsTr("Excused") },
                                { "value": "injured", "text": qsTr("Injured") }
                            ]
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        title: qsTr("ProfileTile")

                        Row {
                            spacing: Theme.spacing.m

                            ProfileTile {
                                id: sampleProfileTile

                                name: qsTr("Jan Kowalski")
                                roleName: qsTr("Coach")
                                initials: "JK"
                                selected: true
                                onClicked: sampleProfileTile.selected = !sampleProfileTile.selected
                            }

                            ProfileTile {
                                name: qsTr("Anna Nowak")
                                roleName: qsTr("Physio")
                                initials: "AN"
                                enabled: false
                            }
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
                        Accessible.name: qsTr("Availability filter")
                        model: root.tableFilterOptions
                        currentValue: root.tableFilterValue
                        onActivated: value => root.tableFilterValue = String(value)
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
                            root.tableFilterValue = "all";
                        }
                    }
                }

                CalendarWeekGrid {
                    objectName: "sampleWeekGrid"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4 * Theme.rowHeight
                    model: root.sampleWeek
                    onItemActivated: (kind, itemId) => root.lastActionText = qsTr("Opened %1 %2").arg(kind).arg(itemId)
                }

                DataTable {
                    id: squadTable

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Accessible.name: qsTr("Sample squad table")
                    model: root.filteredPlayers
                    sortRole: root.tableSortRole
                    sortAscending: root.tableSortAscending
                    onSortRequested: (role, ascending) => {
                        root.tableSortRole = role;
                        root.tableSortAscending = ascending;
                    }
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

            // Tab 3: LoadStatusBadge, ChartLegend, ChartTooltip and LoadChart
            ColumnLayout {
                spacing: Theme.spacing.l

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.l

                    Card {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        title: qsTr("LoadStatusBadge")

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacing.m

                            Repeater {
                                model: [
                                    {
                                        "key": "high",
                                        "text": qsTr("High load")
                                    },
                                    {
                                        "key": "elevated",
                                        "text": qsTr("Elevated")
                                    },
                                    {
                                        "key": "normal",
                                        "text": qsTr("In range")
                                    },
                                    {
                                        "key": "underloaded",
                                        "text": qsTr("Underloaded")
                                    },
                                    {
                                        "key": "notEnoughData",
                                        "text": qsTr("Not enough data")
                                    }
                                ]

                                delegate: LoadStatusBadge {
                                    required property var modelData

                                    statusKey: modelData.key
                                    text: modelData.text
                                }
                            }
                        }

                        ChartLegend {
                            Layout.fillWidth: true
                            items: [
                                {
                                    "label": qsTr("Bar"),
                                    "color": Theme.chart.series1,
                                    "shape": "bar"
                                },
                                {
                                    "label": qsTr("Outlined bar"),
                                    "color": Theme.chart.series3,
                                    "shape": "barOutline"
                                },
                                {
                                    "label": qsTr("Line"),
                                    "color": Theme.chart.series2,
                                    "shape": "line"
                                },
                                {
                                    "label": qsTr("Dashed line"),
                                    "color": Theme.chart.series4,
                                    "shape": "dashedLine"
                                },
                                {
                                    "label": qsTr("Band"),
                                    "color": Theme.chart.bandElevated,
                                    "borderColor": Theme.warn,
                                    "shape": "band"
                                }
                            ]
                        }
                    }

                    Card {
                        Layout.preferredWidth: Theme.chart.tooltipWidth + 2 * Theme.spacing.xl
                        Layout.preferredHeight: tooltipSample.height + Theme.spacing.xxl * 2
                        title: qsTr("ChartTooltip")

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ChartTooltip {
                                id: tooltipSample

                                title: qsTr("Sample day")
                                rows: [
                                    {
                                        "label": qsTr("Training"),
                                        "value": "450",
                                        "color": Theme.chart.series1,
                                        "shape": "bar"
                                    },
                                    {
                                        "label": qsTr("Acute load"),
                                        "value": "368",
                                        "color": Theme.chart.series2,
                                        "shape": "line"
                                    }
                                ]
                                visible: true
                            }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: qsTr("LoadChart (sample data, hover a day)")

                    LoadChart {
                        objectName: "galleryLoadChart"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        dayLabels: root.sampleLoad.labels
                        sessionLoads: root.sampleLoad.sessions
                        matchLoads: root.sampleLoad.matches
                        acutePoints: root.samplePoints(root.sampleLoad.acute, 0)
                        chronicPoints: root.samplePoints(root.sampleLoad.chronic, 0)
                        ratioPoints: root.samplePoints(root.sampleLoad.ratio, 14)
                        loadAxisMax: 1000
                        loadAxisStep: 250
                        ratioAxisMax: 2.5
                        tooltipAt: index => ({
                                    "title": root.sampleLoad.labels[index],
                                    "rows": [
                                        {
                                            "key": "sessions",
                                            "label": qsTr("Training"),
                                            "value": String(root.sampleLoad.sessions[index])
                                        },
                                        {
                                            "key": "matches",
                                            "label": qsTr("Match"),
                                            "value": String(root.sampleLoad.matches[index])
                                        }
                                    ]
                                })
                    }
                }
            }

            // Tab 4: the tactics board (P12) on constant sample values: TacticsPitch, PlayerToken in
            // every state, BenchList, LineupPlayerList and LineupIssuesPanel.
            RowLayout {
                spacing: Theme.spacing.l

                TacticsPitch {
                    objectName: "galleryPitch"
                    Layout.fillHeight: true
                    Layout.preferredWidth: height * Theme.pitch.aspect
                    model: samplePitch
                    onSlotActivated: slot => Toasts.info(qsTr("Activated slot: %1").arg(slot))
                    onSlotsSwapped: (from, to) => Toasts.info(qsTr("Swap: %1 and %2").arg(from).arg(to))
                    onPlayerDropped: (slot, playerId) => Toasts.info(qsTr("Player %1 on slot %2").arg(playerId).arg(slot))
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacing.l

                    Card {
                        Layout.fillWidth: true
                        title: qsTr("PlayerToken")

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: tokenRow.implicitHeight + 2 * Theme.spacing.m
                            radius: Theme.radius.m
                            color: Theme.pitch.grass

                            Row {
                                id: tokenRow

                                anchors.centerIn: parent
                                spacing: Theme.spacing.m

                                PlayerToken {
                                    positionText: "CM"
                                    shirtNumber: 8
                                    shortName: "Kowalczyk"
                                    fullName: "Piotr Kowalczyk"
                                }

                                PlayerToken {
                                    positionText: "ST"
                                    shirtNumber: 9
                                    shortName: "Wiśniewski"
                                    fullName: "Tomasz Wiśniewski"
                                    selected: true
                                    yellowCards: 1
                                    goals: 2
                                }

                                PlayerToken {
                                    positionText: "LW"
                                    shirtNumber: 11
                                    shortName: "Zieliński"
                                    fullName: "Marek Zieliński"
                                    severity: "warning"
                                    issueText: qsTr("Marek Zieliński plays out of position: Left winger.")
                                }

                                PlayerToken {
                                    positionText: "CB"
                                    shirtNumber: 4
                                    shortName: "Szczepaniak-Wojciechowski"
                                    fullName: "Jan Szczepaniak-Wojciechowski"
                                    severity: "error"
                                    issueText: qsTr("Jan Szczepaniak-Wojciechowski is not available on the match day: injured.")
                                }

                                PlayerToken {
                                    positionText: "GK"
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: Theme.spacing.l

                        BenchList {
                            Layout.preferredWidth: Theme.benchColumnWidth
                            Layout.fillHeight: true
                            model: sampleBench
                            onPickRequested: playerId => Toasts.info(qsTr("Pick: %1").arg(playerId))
                            onRemoveRequested: playerId => Toasts.info(qsTr("Remove: %1").arg(playerId))
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: Theme.spacing.m

                            LineupPlayerList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: samplePlayers
                                onPickRequested: playerId => Toasts.info(qsTr("Pick: %1").arg(playerId))
                                onBenchRequested: playerId => Toasts.info(qsTr("To the bench: %1").arg(playerId))
                            }

                            LineupIssuesPanel {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.issuesPanelHeight
                                model: sampleIssues
                                summary: qsTr("Errors: %1 · Warnings: %2").arg(1).arg(2)
                                errorCount: 1
                                onIssueActivated: row => Toasts.info(qsTr("Issue: %1").arg(row))
                            }
                        }
                    }
                }
            }

            // Tab 5: Card, EmptyState, AppDialog and Toasts
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

                // Dashboard (P15): a card in its three states and the locked state of a section.
                Card {
                    objectName: "galleryDashboard"
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    title: qsTr("Dashboard: DashboardCard, DashboardRow, LockedState")

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.l

                        DashboardCard {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            title: qsTr("Unavailable: %1").arg(1)
                            iconName: "squad"

                            DashboardRow {
                                Layout.fillWidth: true
                                text: qsTr("Sample player")

                                PlayerStatusBadge {
                                    statusKey: "injured"
                                    text: qsTr("Injury")
                                }
                            }
                        }

                        DashboardCard {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            title: qsTr("Today's training")
                            iconName: "training"
                            cardState: "empty"
                            emptyText: qsTr("No session today. Plan sessions in the calendar.")
                            emptyActionText: qsTr("Open the calendar")
                        }

                        DashboardCard {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            title: qsTr("Load signals")
                            iconName: "load"
                            cardState: "locked"
                            lockHint: qsTr("Enter RPE after sessions to see load signals and trends. Days with RPE still needed: %1.").arg(9)
                            lockAskText: qsTr("Ask the coach, the assistant or the physio to enter RPE after sessions.")
                        }
                    }

                    LockedState {
                        Layout.fillWidth: true
                        title: qsTr("Pre-match")
                        hint: qsTr("Add players to use the tactics board and the lineup validator. Players still needed: %1.").arg(4)
                        actionText: qsTr("Add players")
                    }
                }

                // Match Day (P13): the score and the clock, the player tiles in every state and the
                // timeline, on constant sample values.
                Card {
                    objectName: "galleryMatchDay"
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    title: qsTr("Match Day: MatchClockBar, MatchPlayerTile, MatchTimeline")

                    MatchClockBar {
                        Layout.fillWidth: true
                        teamName: "KS Przykład"
                        opponentName: "Victoria Wzór"
                        goalsFor: 2
                        goalsAgainst: 1
                        clockText: "45+2:10"
                        phaseText: qsTr("1st half")
                        onPitchCount: 7

                        PrimaryButton {
                            touch: true
                            text: qsTr("End 1st half")
                            onClicked: Toasts.info(qsTr("Clock step"))
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.l

                        Flow {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: Theme.spacing.s

                            MatchPlayerTile {
                                shirtNumber: 9
                                shortName: "Gol"
                                fullName: "Igor Gol"
                                positionCode: "ST"
                                stateText: qsTr("On the pitch")
                                minutesText: "47'"
                                goals: 2
                                onClicked: Toasts.info(qsTr("Tile: %1").arg(fullName))
                            }

                            MatchPlayerTile {
                                shirtNumber: 4
                                shortName: "Szczepaniak-Wojciechowski"
                                fullName: "Jan Szczepaniak-Wojciechowski"
                                positionCode: "CB"
                                stateText: qsTr("On the pitch")
                                minutesText: "47'"
                                yellowCards: 1
                                choosing: true
                                selectable: true
                                onClicked: Toasts.info(qsTr("Tile: %1").arg(fullName))
                            }

                            MatchPlayerTile {
                                shirtNumber: 14
                                shortName: "Rezerwa"
                                fullName: "Leon Rezerwa"
                                positionCode: "CM"
                                stateKey: "bench"
                                stateText: qsTr("On the bench")
                                choosing: true
                                onClicked: Toasts.info(qsTr("Tile: %1").arg(fullName))
                            }

                            MatchPlayerTile {
                                shirtNumber: 7
                                shortName: "Skrzydło"
                                fullName: "Filip Skrzydło"
                                positionCode: "RM"
                                stateKey: "substitutedOff"
                                stateText: qsTr("Substituted")
                                minutesText: "45'"
                                onClicked: Toasts.info(qsTr("Tile: %1").arg(fullName))
                            }

                            MatchPlayerTile {
                                shirtNumber: 3
                                shortName: "Mur"
                                fullName: "Cezary Mur"
                                positionCode: "CB"
                                stateKey: "sentOff"
                                stateText: qsTr("Sent off")
                                minutesText: "38'"
                                yellowCards: 2
                                onClicked: Toasts.info(qsTr("Tile: %1").arg(fullName))
                            }
                        }

                        MatchTimeline {
                            Layout.preferredWidth: Theme.matchTimelineWidth
                            Layout.preferredHeight: 5 * Theme.matchTileHeight
                            model: sampleTimeline
                            notesEditable: true
                            onNoteEditRequested: noteId => Toasts.info(qsTr("Edit note: %1").arg(noteId))
                            onNoteDeleteRequested: noteId => Toasts.info(qsTr("Delete note: %1").arg(noteId))
                        }
                    }
                }

                // Match report samples (P14).
                Card {
                    objectName: "galleryMatchReport"
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    // MatchCorrectionDialog needs the services (MatchReportViewModel), which the gallery
                    // does not have in qml_components: an exception of P14 decision 15.
                    title: qsTr("Match report: MatchRatingsGrid, MatchTimeline with corrections")

                    ListModel {
                        id: sampleRatings

                        ListElement { playerId: 1; shirtNumber: 1; playerName: "Bramkarz Pewny"; positionCode: "GK"; started: true; minutes: 90; minutesText: "90'"; goals: 0; assists: 0; yellowCards: 0; redCards: 0; rating: 7; rpe: 5; rowError: ""; changed: false }
                        ListElement { playerId: 9; shirtNumber: 9; playerName: "Igor Gol"; positionCode: "ST"; started: true; minutes: 90; minutesText: "90'"; goals: 2; assists: 0; yellowCards: 0; redCards: 0; rating: 10; rpe: 8; rowError: ""; changed: true }
                        ListElement { playerId: 3; shirtNumber: 3; playerName: "Cezary Mur"; positionCode: "CB"; started: true; minutes: 38; minutesText: "38'"; goals: 0; assists: 0; yellowCards: 2; redCards: 1; rating: 0; rpe: 6; rowError: qsTr("Rate from 1 to 10: type 1 and 0 quickly for 10."); changed: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.l

                        MatchRatingsGrid {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.tableHeaderHeight + 4 * Theme.rowHeight
                            Accessible.name: qsTr("Sample ratings")
                            model: sampleRatings
                            onDigitEntered: (row, column, digit) => Toasts.info(qsTr("Row %1, column %2: %3").arg(row).arg(column).arg(digit))
                        }

                        MatchTimeline {
                            Layout.preferredWidth: Theme.matchTimelineWidth
                            Layout.preferredHeight: 4 * Theme.matchTileHeight
                            model: sampleTimeline
                            notesEditable: true
                            eventsDeletable: true
                            tacticsDeletable: true
                            onEventDeleteRequested: eventId => Toasts.info(qsTr("Delete event: %1").arg(eventId))
                        }
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

    // Match Day samples (P13): the newest event first.
    ListModel {
        id: sampleTimeline

        ListElement { kind: "note"; eventId: 0; noteId: 9; minuteText: "45+3'"; typeKey: "note"; iconName: "note"; typeText: qsTr("Note"); playerText: "Cezary Mur"; relatedText: qsTr("Ankle after the tackle, watch him in the break."); noteMinute: "45+3"; notePlayerId: 3; noteText: qsTr("Ankle after the tackle, watch him in the break."); changeId: 0 }
        ListElement { kind: "event"; eventId: 4; noteId: 0; minuteText: "45+2'"; typeKey: "secondYellow"; iconName: "card"; typeText: qsTr("Second yellow, red card"); playerText: "Cezary Mur"; relatedText: ""; noteMinute: ""; notePlayerId: 0; noteText: ""; changeId: 0 }
        ListElement { kind: "event"; eventId: 3; noteId: 0; minuteText: "38'"; typeKey: "sub"; iconName: "swap"; typeText: qsTr("Substitution"); playerText: qsTr("On: Leon Rezerwa"); relatedText: qsTr("Off: Filip Skrzydło"); noteMinute: ""; notePlayerId: 0; noteText: ""; changeId: 0 }
        ListElement { kind: "event"; eventId: 2; noteId: 0; minuteText: "23'"; typeKey: "opponentGoal"; iconName: "goal"; typeText: qsTr("Opponent goal"); playerText: "Victoria Wzór"; relatedText: ""; noteMinute: ""; notePlayerId: 0; noteText: ""; changeId: 0 }
        ListElement { kind: "event"; eventId: 1; noteId: 0; minuteText: "11'"; typeKey: "goal"; iconName: "goal"; typeText: qsTr("Goal"); playerText: "Igor Gol"; relatedText: qsTr("Assist: Jan Szczepaniak-Wojciechowski"); noteMinute: ""; notePlayerId: 0; noteText: ""; changeId: 0 }
    }

    // Tactics board samples (P12): a 4-4-2 with one free slot, the coordinates of its zone layout.
    ListModel {
        id: samplePitch

        ListElement { slotIndex: 0; positionText: "GK"; pitchX: 0.5; pitchY: 0.94; playerId: 1; shirtNumber: 1; shortName: "Bramka"; fullName: "Adam Bramka"; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 1; positionText: "RB"; pitchX: 0.875; pitchY: 0.8; playerId: 2; shirtNumber: 2; shortName: "Prawy"; fullName: "Bartosz Prawy"; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 2; positionText: "CB"; pitchX: 0.375; pitchY: 0.8; playerId: 3; shirtNumber: 3; shortName: "Mur"; fullName: "Cezary Mur"; severity: "error"; issueText: qsTr("Cezary Mur is not available on the match day: injured."); selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 3; positionText: "CB"; pitchX: 0.625; pitchY: 0.8; playerId: 4; shirtNumber: 4; shortName: "Skała"; fullName: "Damian Skała"; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 4; positionText: "LB"; pitchX: 0.125; pitchY: 0.8; playerId: 5; shirtNumber: 5; shortName: "Lewy"; fullName: "Emil Lewy"; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 5; positionText: "RM"; pitchX: 0.875; pitchY: 0.48; playerId: 6; shirtNumber: 7; shortName: "Skrzydło"; fullName: "Filip Skrzydło"; severity: "warning"; issueText: qsTr("Filip Skrzydło has a high load. A signal to talk to the player."); selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 6; positionText: "CM"; pitchX: 0.375; pitchY: 0.48; playerId: 7; shirtNumber: 6; shortName: "Środek"; fullName: "Grzegorz Środek"; severity: ""; issueText: ""; selected: true; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 7; positionText: "CM"; pitchX: 0.625; pitchY: 0.48; playerId: 0; shirtNumber: 0; shortName: ""; fullName: ""; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 8; positionText: "LM"; pitchX: 0.125; pitchY: 0.48; playerId: 9; shirtNumber: 11; shortName: "Kowalczyk-Nowakowski"; fullName: "Hubert Kowalczyk-Nowakowski"; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 9; positionText: "ST"; pitchX: 0.375; pitchY: 0.12; playerId: 10; shirtNumber: 9; shortName: "Gol"; fullName: "Igor Gol"; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
        ListElement { slotIndex: 10; positionText: "ST"; pitchX: 0.625; pitchY: 0.12; playerId: 11; shirtNumber: 10; shortName: "Strzał"; fullName: "Jakub Strzał"; severity: ""; issueText: ""; selected: false; yellowCards: 0; goals: 0; hidden: false; labelRoom: 1 }
    }

    ListModel {
        id: sampleBench

        ListElement { benchIndex: 0; playerId: 12; shirtNumber: 12; name: "Kamil Zapas"; shortName: "Zapas"; positionsText: "GK"; severity: ""; issueText: "" }
        ListElement { benchIndex: 1; playerId: 13; shirtNumber: 14; name: "Leon Rezerwa"; shortName: "Rezerwa"; positionsText: "CM (DM)"; severity: "warning"; issueText: qsTr("Leon Rezerwa missed each of the last training sessions: 3.") }
        ListElement { benchIndex: 2; playerId: 14; shirtNumber: 18; name: "Michał Zmiennik"; shortName: "Zmiennik"; positionsText: "ST (RW, LW)"; severity: ""; issueText: "" }
    }

    ListModel {
        id: samplePlayers

        ListElement { playerId: 1; shirtNumber: 1; name: "Adam Bramka"; shortName: "Bramka"; positionsText: "GK"; statusKey: "available"; statusText: qsTr("Available"); loadStatusKey: "normal"; loadStatusText: qsTr("In range"); placement: "xi"; placementText: qsTr("In the XI"); severity: ""; issueText: ""; detailsText: qsTr("On the match day: available. Load as of 31.10 (today): in range."); section: 0; sectionText: qsTr("Starting XI") }
        ListElement { playerId: 3; shirtNumber: 3; name: "Cezary Mur"; shortName: "Mur"; positionsText: "CB"; statusKey: "injured"; statusText: qsTr("Injured"); loadStatusKey: "notEnoughData"; loadStatusText: qsTr("No baseline"); placement: "xi"; placementText: qsTr("In the XI"); severity: "error"; issueText: ""; detailsText: qsTr("On the match day: available. Load as of 31.10 (today): in range."); section: 0; sectionText: qsTr("Starting XI") }
        ListElement { playerId: 6; shirtNumber: 7; name: "Filip Skrzydło"; shortName: "Skrzydło"; positionsText: "RM (RW)"; statusKey: "available"; statusText: qsTr("Available"); loadStatusKey: "high"; loadStatusText: qsTr("High"); placement: "xi"; placementText: qsTr("In the XI"); severity: "warning"; issueText: ""; detailsText: qsTr("On the match day: available. Load as of 31.10 (today): in range."); section: 0; sectionText: qsTr("Starting XI") }
        ListElement { playerId: 15; shirtNumber: 21; name: "Norbert Wolny"; shortName: "Wolny"; positionsText: "CM"; statusKey: "suspended"; statusText: qsTr("Suspended"); loadStatusKey: ""; loadStatusText: ""; placement: ""; placementText: ""; severity: ""; issueText: ""; detailsText: qsTr("On the match day: available. Load as of 31.10 (today): in range."); section: 2; sectionText: qsTr("Not in the lineup") }
    }

    ListModel {
        id: sampleIssues

        ListElement { severity: "error"; severityText: qsTr("Error"); ruleKey: "unavailable"; text: qsTr("Cezary Mur is not available on the match day: injured."); playerId: 3; slotIndex: 2; benchIndex: -1 }
        ListElement { severity: "warning"; severityText: qsTr("Warning"); ruleKey: "highLoad"; text: qsTr("Filip Skrzydło has a high load. A signal to talk to the player."); playerId: 6; slotIndex: 5; benchIndex: -1 }
        ListElement { severity: "warning"; severityText: qsTr("Warning"); ruleKey: "missedSessions"; text: qsTr("Leon Rezerwa missed each of the last training sessions: 3."); playerId: 13; slotIndex: -1; benchIndex: 1 }
    }
}
