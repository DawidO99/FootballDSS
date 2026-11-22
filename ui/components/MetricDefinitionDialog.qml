import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Adds or edits a custom metric definition (packet P17). Enter saves from any field. A metric with
// values keeps its type and what it is entered for, so those fields are disabled with a hint.
// API:
//   viewModel: MetricDefinitionsViewModel   required; does the add and the update
//   openForAdding(name, scope)  an empty form; `name` prefills both names and `scope` ("training"
//                           by default) what it is entered for (an import column, P18)
//   openForEditing(row)     a row of MetricDefinitionListModel.rowAt()
//   editedId: double        read-only, 0 while adding
//   saved()
AppDialog {
    id: root

    required property MetricDefinitionsViewModel viewModel
    property double editedId: 0
    property bool locked: false
    property string valueType: "decimal"
    property string aggregation: "avg"
    property string scope: "training"
    readonly property bool boolean: root.valueType === "boolean"
    readonly property var errors: root.viewModel.fieldErrors
    readonly property var aggregationOptions: root.viewModel.aggregationOptions(root.valueType)

    signal saved

    function show(namePl, nameEn, valueType, unit, minText, maxText, aggregation, scope, higherIsBetter) {
        root.viewModel.clearErrors();
        namePlInput.text = namePl;
        nameEnInput.text = nameEn;
        root.valueType = valueType;
        unitInput.text = unit;
        minInput.text = minText;
        maxInput.text = maxText;
        root.aggregation = aggregation;
        root.scope = scope;
        higherInput.checked = higherIsBetter;
        root.open();
        namePlInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function openForAdding(name, scope) {
        root.editedId = 0;
        root.locked = false;
        root.show(name ?? "", name ?? "", "decimal", "", "", "", "avg", scope ?? "training", true);
    }

    function openForEditing(row) {
        root.editedId = row.metricId;
        root.locked = root.viewModel.isLocked(row.metricId);
        root.show(row.namePl, row.nameEn, row.valueType, row.unit, row.minText, row.maxText, row.aggregation, row.scope, row.higherIsBetter);
    }

    // A yes/no metric has no limits and only some aggregations.
    function chooseType(value) {
        root.valueType = value;
        const options = root.viewModel.aggregationOptions(value);
        if (!options.some(option => option.value === root.aggregation))
            root.aggregation = options[0].value;
        if (value === "boolean") {
            minInput.text = "";
            maxInput.text = "";
        }
    }

    function form() {
        return {
            "namePl": namePlInput.text,
            "nameEn": nameEnInput.text,
            "valueType": root.valueType,
            "unit": unitInput.text,
            "minText": minInput.text,
            "maxText": maxInput.text,
            "aggregation": root.aggregation,
            "scope": root.scope,
            "higherIsBetter": higherInput.checked
        };
    }

    function focusFirstError() {
        const fields = [["namePl", namePlInput], ["nameEn", nameEnInput], ["unit", unitInput], ["minText", minInput], ["maxText", maxInput], ["aggregation", aggregationInput]];
        for (const field of fields) {
            if (root.errors[field[0]] !== undefined) {
                field[1].forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }

    objectName: "metricDefinitionDialog"
    title: root.editedId === 0 ? qsTr("Add metric") : qsTr("Edit metric")
    primaryText: qsTr("Save")
    onPrimaryClicked: {
        const done = root.editedId === 0 ? root.viewModel.add(root.form()) : root.viewModel.update(root.editedId, root.form());
        if (done) {
            root.close();
            root.saved();
        } else {
            root.focusFirstError();
        }
    }
    onRejected: root.viewModel.clearErrors()

    Label {
        Layout.fillWidth: true
        text: qsTr("One name is enough; the other language shows it too.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.caption
        wrapMode: Text.Wrap
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        rowSpacing: Theme.spacing.m
        columnSpacing: Theme.spacing.l

        FormField {
            id: namePlField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Name in Polish")
            error: root.errors.namePl ?? ""

            TextInputField {
                id: namePlInput

                objectName: "metricNamePlInput"
                Layout.fillWidth: true
                maximumLength: 50
                Accessible.name: namePlField.label
                onAccepted: root.triggerPrimary()
            }
        }

        FormField {
            id: nameEnField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Name in English")
            error: root.errors.nameEn ?? ""

            TextInputField {
                id: nameEnInput

                objectName: "metricNameEnInput"
                Layout.fillWidth: true
                maximumLength: 50
                Accessible.name: nameEnField.label
                onAccepted: root.triggerPrimary()
            }
        }

        FormField {
            id: typeField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Type")
            mandatory: true

            SelectField {
                id: typeInput

                objectName: "metricTypeInput"
                Layout.fillWidth: true
                enabled: !root.locked
                textRole: "text"
                valueRole: "value"
                model: root.viewModel.valueTypeOptions
                value: root.valueType
                Accessible.name: typeField.label
                onValueActivated: value => root.chooseType(value)
            }
        }

        FormField {
            id: scopeField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Entered for")
            mandatory: true

            SelectField {
                id: scopeInput

                objectName: "metricScopeInput"
                Layout.fillWidth: true
                enabled: !root.locked
                textRole: "text"
                valueRole: "value"
                model: root.viewModel.scopeOptions
                value: root.scope
                Accessible.name: scopeField.label
                onValueActivated: value => root.scope = value
            }
        }

        FormField {
            id: unitField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            visible: !root.boolean
            label: qsTr("Unit")
            helpText: qsTr("For example s, cm or kg")
            error: root.errors.unit ?? ""

            TextInputField {
                id: unitInput

                objectName: "metricUnitInput"
                Layout.fillWidth: true
                maximumLength: 20
                Accessible.name: unitField.label
                onAccepted: root.triggerPrimary()
            }
        }

        FormField {
            id: aggregationField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            label: qsTr("Over 28 days")
            helpText: qsTr("What the player card shows")
            mandatory: true
            error: root.errors.aggregation ?? ""

            SelectField {
                id: aggregationInput

                objectName: "metricAggregationInput"
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: root.aggregationOptions
                value: root.aggregation
                Accessible.name: aggregationField.label
                onValueActivated: value => root.aggregation = value
            }
        }

        FormField {
            id: minField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            visible: !root.boolean
            label: qsTr("Minimum")
            helpText: qsTr("Optional")
            error: root.errors.minText ?? ""

            TextInputField {
                id: minInput

                objectName: "metricMinInput"
                Layout.fillWidth: true
                maximumLength: 18
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                Accessible.name: minField.label
                onAccepted: root.triggerPrimary()
            }
        }

        FormField {
            id: maxField

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            visible: !root.boolean
            label: qsTr("Maximum")
            helpText: qsTr("Optional")
            error: root.errors.maxText ?? ""

            TextInputField {
                id: maxInput

                objectName: "metricMaxInput"
                Layout.fillWidth: true
                maximumLength: 18
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                Accessible.name: maxField.label
                onAccepted: root.triggerPrimary()
            }
        }
    }

    CheckBoxField {
        id: higherInput

        objectName: "metricHigherInput"
        text: qsTr("Higher is better")
        checked: true
    }

    Label {
        objectName: "metricLockedHint"
        Layout.fillWidth: true
        visible: root.locked
        text: qsTr("This metric already has values, so its type and what it is entered for stay.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize.caption
        wrapMode: Text.Wrap
    }
}
