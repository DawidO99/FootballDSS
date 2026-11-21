pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import FootballDSS

// The import wizard (SPEC section 8, packet P16): type -> file -> preview -> mapping -> validation
// report -> import -> summary with "Undo import". Nothing is written before the import. The
// roles with Import use it; the others are told who can.
Item {
    id: root

    // What the section was opened with, see NavigationModel::activate().
    property var navigationArgs: ({})
    readonly property alias viewModel: wizard
    readonly property bool canImport: AppState.can("Import")
    readonly property var stepKeys: ["type", "file", "preview", "mapping", "report", "summary"]
    readonly property int stepIndex: Math.max(0, stepKeys.indexOf(wizard.step))
    readonly property bool dialogOpen: undoDialog.visible
    readonly property alias undoImportDialog: undoDialog

    signal navigateRequested(string key, var args)

    function chooseFile() {
        if (wizard.step === "file" && !wizard.busy)
            openDialog.open();
    }

    function saveTemplate(format) {
        templateDialog.format = format;
        templateDialog.open();
    }

    // One line of what went wrong: icon + text, never color-only.
    component ProblemLine: RowLayout {
        id: problem

        property string text
        property string iconName: "danger"
        property color tone: Theme.danger

        spacing: Theme.spacing.s
        visible: text.length > 0

        Icon {
            Layout.alignment: Qt.AlignTop
            name: problem.iconName
            color: problem.tone
            size: Theme.iconSize.s
        }

        Text {
            Layout.fillWidth: true
            text: problem.text
            textFormat: Text.PlainText
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }

    ImportWizardViewModel {
        id: wizard

        teamId: AppState.teamId
        onErrorOccurred: message => Toasts.error(message)
        onInfoMessage: message => Toasts.info(message)
    }

    Shortcut {
        id: openShortcut

        objectName: "importOpenShortcut"
        sequence: "Ctrl+O"
        enabled: root.visible && root.canImport && wizard.step === "file" && !wizard.busy
        onActivated: root.chooseFile()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Import")
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.canImport
            iconName: "import"
            title: qsTr("Import is not available for your role")
            description: qsTr("Ask the coach, an assistant or the analyst to import the data.")
        }

        ImportStepIndicator {
            objectName: "importSteps"
            Layout.fillWidth: true
            visible: root.canImport
            currentIndex: root.stepIndex
            steps: [qsTr("Type"), qsTr("File"), qsTr("Preview"), qsTr("Columns"), qsTr("Check"), qsTr("Done")]
        }

        StackLayout {
            id: pages

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.canImport
            currentIndex: root.stepIndex

            // --- Type ---
            ColumnLayout {
                spacing: Theme.spacing.l

                Card {
                    Layout.fillWidth: true
                    title: qsTr("What do you want to import?")

                    Repeater {
                        model: wizard.typeOptions

                        delegate: RowLayout {
                            id: typeRow

                            required property var modelData

                            Layout.fillWidth: true
                            spacing: Theme.spacing.l

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacing.xs

                                Text {
                                    text: typeRow.modelData.text
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSize.title
                                    font.bold: true
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: typeRow.modelData.description
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSize.body
                                    wrapMode: Text.Wrap
                                }
                            }

                            PrimaryButton {
                                objectName: "importType_" + typeRow.modelData.value
                                text: qsTr("Choose")
                                onClicked: wizard.chooseType(typeRow.modelData.value)
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Training sessions and matches will be imported here too in a later version.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                        wrapMode: Text.Wrap
                    }
                }

                Card {
                    Layout.fillWidth: true
                    title: qsTr("Templates")

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Start from a template if you have no file yet: it has every column the import recognises. Positions may be written as BR, ŚO, N or as GK, CB, ST.")
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        spacing: Theme.spacing.m

                        SecondaryButton {
                            objectName: "saveXlsxTemplate"
                            text: qsTr("Save the squad template (XLSX)")
                            onClicked: root.saveTemplate("xlsx")
                        }

                        SecondaryButton {
                            objectName: "saveCsvTemplate"
                            text: qsTr("Save the squad template (CSV)")
                            onClicked: root.saveTemplate("csv")
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            // --- File ---
            ColumnLayout {
                spacing: Theme.spacing.l

                Card {
                    Layout.fillWidth: true
                    title: qsTr("Choose the file")

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("A CSV, TXT, TSV or XLSX file of at most 20 MB, for example the list of players you keep in Excel. The first row with text must hold the column names. Nothing is saved until you confirm the import.")
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        wrapMode: Text.Wrap
                    }

                    PrimaryButton {
                        objectName: "chooseFileButton"
                        text: qsTr("Choose file…")
                        enabled: !wizard.busy
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Choose file (%1)").arg(openShortcut.nativeText)
                        ToolTip.delay: Theme.tooltipDelay
                        onClicked: root.chooseFile()
                    }

                    ProgressLine {
                        Layout.fillWidth: true
                        visible: wizard.busy
                        value: wizard.progress
                        text: qsTr("Reading the file…")
                    }

                    ProblemLine {
                        objectName: "fileError"
                        Layout.fillWidth: true
                        text: wizard.errorText
                    }
                }

                Item {
                    Layout.fillHeight: true
                }

                RowLayout {
                    SecondaryButton {
                        text: qsTr("Back")
                        enabled: !wizard.busy
                        onClicked: wizard.back()
                    }
                }
            }

            // --- Preview ---
            ColumnLayout {
                spacing: Theme.spacing.m

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.l

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.xs

                        Text {
                            Layout.fillWidth: true
                            text: wizard.fileName
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: Theme.fontSize.title
                            font.bold: true
                            elide: Text.ElideMiddle
                        }

                        Text {
                            Layout.fillWidth: true
                            text: wizard.sourceSummary
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSize.body
                            elide: Text.ElideRight
                        }
                    }

                    FormField {
                        id: sheetField

                        visible: wizard.sheetOptions.length > 1
                        label: qsTr("Sheet")

                        SelectField {
                            objectName: "sheetChoice"
                            Layout.preferredWidth: Theme.fieldWidth
                            model: wizard.sheetOptions
                            textRole: "text"
                            valueRole: "value"
                            value: wizard.sheetIndex
                            enabled: !wizard.busy
                            Accessible.name: sheetField.label
                            onValueActivated: chosen => wizard.selectSheet(chosen)
                        }
                    }
                }

                ProblemLine {
                    objectName: "duplicateWarning"
                    Layout.fillWidth: true
                    text: wizard.duplicateWarning
                    iconName: "warn"
                    tone: Theme.warn
                }

                ProblemLine {
                    objectName: "previewError"
                    Layout.fillWidth: true
                    text: wizard.errorText
                }

                ProgressLine {
                    Layout.fillWidth: true
                    visible: wizard.busy
                    value: wizard.progress
                    text: qsTr("Reading the file…")
                }

                Text {
                    Layout.fillWidth: true
                    visible: wizard.rowCount > 0
                    text: wizard.rowCount > wizard.previewRows.length ? qsTr("The first %1 of %2 rows. Check that the columns are split right and the Polish letters look right.").arg(wizard.previewRows.length).arg(wizard.rowCount) : qsTr("Check that the columns are split right and the Polish letters look right.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.Wrap
                }

                DataTable {
                    objectName: "previewTable"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: wizard.previewRows
                    columns: {
                        const all = wizard.previewColumns;
                        const fraction = all.length > 1 ? 1 / (all.length - 1) : 1;
                        return all.map((column, index) => ({
                                    "role": column.role,
                                    "title": column.title,
                                    "width": index === 0 ? Theme.importRowColumnWidth : fraction,
                                    "sortable": false
                                }));
                    }
                    emptyState: Component {
                        EmptyState {
                            iconName: "import"
                            title: qsTr("No rows to show")
                            description: wizard.sheetOptions.length > 1 ? qsTr("Choose the sheet with the players.") : qsTr("Choose another file.")
                            actionText: qsTr("Choose another file")
                            onActionTriggered: wizard.back()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    SecondaryButton {
                        text: qsTr("Back")
                        enabled: !wizard.busy
                        onClicked: wizard.back()
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    PrimaryButton {
                        objectName: "toMappingButton"
                        text: qsTr("Next: columns")
                        enabled: !wizard.busy && wizard.rowCount > 0
                        onClicked: wizard.goToMapping()
                    }
                }
            }

            // --- Mapping ---
            ColumnLayout {
                spacing: Theme.spacing.m

                FocusFlickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: mappingColumn.implicitHeight

                    ColumnLayout {
                        id: mappingColumn

                        width: parent.width
                        spacing: Theme.spacing.l

                        ProblemLine {
                            Layout.fillWidth: true
                            text: wizard.mappingRemembered ? qsTr("The columns are set as in your last import of a file with these headers.") : ""
                            iconName: "info"
                            tone: Theme.info
                        }

                        ImportMappingTable {
                            id: mappingTable

                            objectName: "mappingTable"
                            Layout.fillWidth: true
                            model: wizard.mapping
                            fieldOptions: wizard.fieldOptions
                            onFieldChosen: (column, field) => wizard.setColumnField(column, field)
                        }

                        FormField {
                            id: nameOrderField

                            Layout.fillWidth: true
                            visible: wizard.nameOrderVisible
                            label: qsTr("Order in the name column")
                            helpText: qsTr("A double last name stays together, e.g. Nowak-Kowalski.")

                            SegmentedControl {
                                objectName: "nameOrderChoice"
                                model: wizard.nameOrderOptions
                                currentValue: wizard.nameOrder
                                Accessible.name: nameOrderField.label
                                onActivated: value => wizard.setNameOrder(value)
                            }

                            Repeater {
                                model: wizard.nameSplitPreview

                                delegate: Text {
                                    required property var modelData

                                    Layout.fillWidth: true
                                    text: modelData.split ? qsTr("%1 → first name: %2, last name: %3").arg(modelData.fullName).arg(modelData.firstName).arg(modelData.lastName) : qsTr("%1 → cannot be split").arg(modelData.fullName)
                                    textFormat: Text.PlainText
                                    color: modelData.split ? Theme.text : Theme.danger
                                    font.pixelSize: Theme.fontSize.body
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        FormField {
                            id: defaultPositionField

                            mandatory: wizard.defaultPositionRequired
                            label: qsTr("Default position")
                            helpText: wizard.defaultPositionRequired ? qsTr("Some players have no position in the file. They get this one with a warning; correct it later in Squad.") : qsTr("Every player has a position in the file; this one is not used.")

                            SelectField {
                                objectName: "defaultPositionChoice"
                                Layout.preferredWidth: Theme.fieldWidth
                                model: wizard.positionOptions
                                textRole: "text"
                                valueRole: "value"
                                value: wizard.defaultPosition
                                placeholderText: qsTr("Choose a position")
                                enabled: wizard.defaultPositionRequired
                                Accessible.name: defaultPositionField.label
                                onValueActivated: chosen => wizard.setDefaultPosition(chosen)
                            }
                        }

                        Repeater {
                            model: wizard.mappingErrors

                            delegate: ProblemLine {
                                required property string modelData

                                Layout.fillWidth: true
                                text: modelData
                            }
                        }
                    }
                }

                ProgressLine {
                    Layout.fillWidth: true
                    visible: wizard.busy
                    value: wizard.progress
                    text: qsTr("Checking the rows… %1%").arg(Math.round(Math.max(0, wizard.progress) * 100))
                }

                RowLayout {
                    Layout.fillWidth: true

                    SecondaryButton {
                        text: qsTr("Back")
                        enabled: !wizard.busy
                        onClicked: wizard.back()
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    PrimaryButton {
                        objectName: "validateButton"
                        text: qsTr("Next: check the rows")
                        enabled: !wizard.busy && wizard.mappingErrors.length === 0
                        onClicked: wizard.validate()
                    }
                }
            }

            // --- Report ---
            ColumnLayout {
                spacing: Theme.spacing.m

                RowLayout {
                    spacing: Theme.spacing.m

                    StatusBadge {
                        kind: StatusBadge.Ok
                        text: qsTr("OK: %1").arg(wizard.okCount)
                    }

                    StatusBadge {
                        kind: StatusBadge.Warn
                        text: qsTr("Warnings: %1").arg(wizard.warningCount)
                    }

                    StatusBadge {
                        kind: StatusBadge.Danger
                        text: qsTr("Errors: %1").arg(wizard.errorCount)
                    }
                }

                ProblemLine {
                    Layout.fillWidth: true
                    text: wizard.duplicateWarning
                    iconName: "warn"
                    tone: Theme.warn
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.xl

                    CheckBoxField {
                        objectName: "updateExistingCheck"
                        text: qsTr("Update players already in the squad")
                        checked: wizard.updateExisting
                        onToggled: wizard.updateExisting = checked
                    }

                    CheckBoxField {
                        objectName: "skipInvalidCheck"
                        text: qsTr("Skip rows with errors")
                        checked: wizard.skipInvalid
                        onToggled: wizard.skipInvalid = checked
                    }
                }

                Text {
                    objectName: "importPlan"
                    Layout.fillWidth: true
                    text: wizard.errorCount > 0 && !wizard.skipInvalid ? qsTr("Nothing will be imported while rows have errors. Fix them in the file, or skip them.") : qsTr("To add: %1. To update: %2. Skipped: %3. With errors: %4.").arg(wizard.createCount).arg(wizard.updateCount).arg(wizard.skipCount).arg(wizard.errorCount)
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.Wrap
                }

                ImportReportTable {
                    objectName: "reportTable"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: wizard.report
                    emptyState: Component {
                        EmptyState {
                            iconName: "import"
                            title: qsTr("No rows were checked")
                            description: qsTr("Go back to the columns and check the rows again.")
                            actionText: qsTr("Back")
                            onActionTriggered: wizard.back()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    SecondaryButton {
                        text: qsTr("Back")
                        onClicked: wizard.back()
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    PrimaryButton {
                        objectName: "commitButton"
                        text: qsTr("Import")
                        enabled: wizard.canCommit
                        onClicked: wizard.commit()
                    }
                }
            }

            // --- Summary ---
            ColumnLayout {
                spacing: Theme.spacing.l

                Card {
                    Layout.fillWidth: true
                    title: wizard.undone ? qsTr("The import was undone") : qsTr("The import is done")

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Added: %1. Updated: %2. Skipped: %3. Not imported because of errors: %4.").arg(wizard.summary.created ?? 0).arg(wizard.summary.updated ?? 0).arg(wizard.summary.skipped ?? 0).arg(wizard.summary.invalid ?? 0)
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        wrapMode: Text.Wrap
                    }

                    ProblemLine {
                        Layout.fillWidth: true
                        text: wizard.undone ? qsTr("Removed: %1. Archived, because they already have data such as attendance or a lineup: %2. Updated players were not changed back: %3.").arg(wizard.summary.deleted ?? 0).arg(wizard.summary.archived ?? 0).arg(wizard.summary.notReverted ?? 0) : qsTr("Undo import removes the players this import added. Players it updated are not changed back.")
                        iconName: "info"
                        tone: Theme.info
                    }

                    RowLayout {
                        spacing: Theme.spacing.m

                        DangerButton {
                            objectName: "undoImportButton"
                            visible: wizard.canUndo
                            text: qsTr("Undo import")
                            onClicked: undoDialog.open()
                        }

                        SecondaryButton {
                            objectName: "openSquadButton"
                            text: qsTr("Open Squad")
                            onClicked: root.navigateRequested("squad", {})
                        }

                        PrimaryButton {
                            objectName: "importAgainButton"
                            text: qsTr("Import another file")
                            onClicked: wizard.reset()
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    FileDialog {
        id: openDialog

        title: qsTr("Choose the file to import")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Tables (*.csv *.txt *.tsv *.xlsx)")]
        onAccepted: wizard.openFile(selectedFile)
    }

    FileDialog {
        id: templateDialog

        property string format: "xlsx"

        title: qsTr("Save the squad template")
        fileMode: FileDialog.SaveFile
        defaultSuffix: format
        nameFilters: format === "xlsx" ? [qsTr("Excel workbooks (*.xlsx)")] : [qsTr("CSV files (*.csv)")]
        onAccepted: wizard.saveTemplate(selectedFile, format)
    }

    AppDialog {
        id: undoDialog

        objectName: "undoImportDialog"

        title: qsTr("Undo this import?")
        size: AppDialog.Small
        primaryText: qsTr("Undo import")
        onPrimaryClicked: {
            wizard.undoImport();
            undoDialog.close();
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("The players this import added are removed, or archived when they already have data. Players it updated stay as they are.")
            color: Theme.text
            font.pixelSize: Theme.fontSize.body
            wrapMode: Text.Wrap
        }
    }
}
