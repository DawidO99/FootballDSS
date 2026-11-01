pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// First run: welcome + language, club and team, coach account, summary. Enter = Next.
FocusScope {
    id: root

    // Exposed for tests (qml_smoke goes through the wizard with it).
    readonly property SetupWizardViewModel viewModel: wizard
    readonly property bool onLastStep: wizard.currentStep === wizard.stepCount - 1

    readonly property var stepTitles: [qsTr("Welcome"), qsTr("Club and team"), qsTr("Coach account"), qsTr("Summary")]
    readonly property var categoryOptions: [
        {
            "value": "senior",
            "text": qsTr("Seniors")
        },
        {
            "value": "u19",
            "text": qsTr("U19")
        },
        {
            "value": "u17",
            "text": qsTr("U17")
        },
        {
            "value": "u15",
            "text": qsTr("U15")
        },
        {
            "value": "u13",
            "text": qsTr("U13")
        },
        {
            "value": "u11",
            "text": qsTr("U11")
        },
        {
            "value": "other",
            "text": qsTr("Other")
        }
    ]

    function advance() {
        if (root.onLastStep)
            wizard.create();
        else if (!wizard.next())
            root.focusFirstError();
    }

    function focusCurrentStep() {
        switch (wizard.currentStep) {
        case 0:
            languageControl.forceActiveFocus(Qt.TabFocusReason);
            break;
        case 1:
            clubNameInput.forceActiveFocus(Qt.TabFocusReason);
            break;
        case 2:
            coachNameInput.forceActiveFocus(Qt.TabFocusReason);
            break;
        default:
            nextButton.forceActiveFocus(Qt.TabFocusReason);
            break;
        }
    }

    function focusFirstError() {
        if (wizard.clubNameError.length > 0)
            clubNameInput.forceActiveFocus(Qt.TabFocusReason);
        else if (wizard.teamNameError.length > 0)
            teamNameInput.forceActiveFocus(Qt.TabFocusReason);
        else if (wizard.seasonError.length > 0)
            seasonInput.forceActiveFocus(Qt.TabFocusReason);
        else if (wizard.coachNameError.length > 0)
            coachNameInput.forceActiveFocus(Qt.TabFocusReason);
        else if (wizard.pinError.length > 0)
            pinInput.forceActiveFocus(Qt.TabFocusReason);
        else if (wizard.pinRepeatError.length > 0)
            pinRepeatInput.forceActiveFocus(Qt.TabFocusReason);
    }

    function categoryName(value) {
        const option = root.categoryOptions.find(candidate => candidate.value === value);
        return option ? option.text : "";
    }

    Keys.onReturnPressed: root.advance()
    Keys.onEnterPressed: root.advance()
    Component.onCompleted: root.focusCurrentStep()

    SetupWizardViewModel {
        id: wizard

        onCurrentStepChanged: root.focusCurrentStep()
        onCompleted: AppState.refresh()
        onErrorOccurred: message => Toasts.error(message)
    }

    LanguageThemeSwitch {
        objectName: "cornerSwitch"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacing.m
        // The first step has its own language control.
        showLanguage: wizard.currentStep > 0
    }

    Card {
        anchors.centerIn: parent
        width: Math.min(Theme.setupCardWidth, parent.width - 2 * Theme.spacing.xl)
        title: root.stepTitles[wizard.currentStep] ?? ""

        actions: Label {
            text: qsTr("Step %1 of %2").arg(wizard.currentStep + 1).arg(wizard.stepCount)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize.caption
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: wizard.currentStep

            ColumnLayout {
                spacing: Theme.spacing.m

                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    text: qsTr("FootballDSS helps you plan training and matches and keep an eye on your squad. A few details and you can start.")
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.WordWrap
                }

                FormField {
                    id: languageFormField

                    Layout.fillWidth: true
                    label: qsTr("Language")

                    SegmentedControl {
                        id: languageControl

                        model: LanguageManager.availableLanguages.map(code => ({
                                    "value": code,
                                    "text": code.toUpperCase()
                                }))
                        currentValue: LanguageManager.language
                        Accessible.name: languageFormField.label
                        onActivated: value => LanguageManager.setLanguage(value)
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                spacing: Theme.spacing.m

                FormField {
                    id: clubNameFormField

                    Layout.fillWidth: true
                    label: qsTr("Club name")
                    mandatory: true
                    error: clubNameInput.effectiveError

                    TextInputField {
                        id: clubNameInput

                        Layout.fillWidth: true
                        text: wizard.clubName
                        error: wizard.clubNameError
                        Accessible.name: clubNameFormField.label
                        onTextEdited: wizard.clubName = text
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

                        Layout.fillWidth: true
                        text: wizard.teamName
                        error: wizard.teamNameError
                        Accessible.name: teamNameFormField.label
                        onTextEdited: wizard.teamName = text
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.m

                    FormField {
                        id: categoryFormField

                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        label: qsTr("Category")

                        SelectField {
                            Layout.fillWidth: true
                            textRole: "text"
                            valueRole: "value"
                            model: root.categoryOptions
                            value: wizard.category
                            Accessible.name: categoryFormField.label
                            onValueActivated: value => wizard.category = value
                        }
                    }

                    FormField {
                        id: seasonFormField

                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        label: qsTr("Season")
                        mandatory: true
                        helpText: qsTr("For example 2026/27.")
                        error: seasonInput.effectiveError

                        TextInputField {
                            id: seasonInput

                            Layout.fillWidth: true
                            text: wizard.season
                            error: wizard.seasonError
                            Accessible.name: seasonFormField.label
                            onTextEdited: wizard.season = text
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                spacing: Theme.spacing.m

                FormField {
                    id: coachNameFormField

                    Layout.fillWidth: true
                    label: qsTr("Your name")
                    mandatory: true
                    helpText: qsTr("You will be the coach, who manages the staff and the settings.")
                    error: coachNameInput.effectiveError

                    TextInputField {
                        id: coachNameInput

                        Layout.fillWidth: true
                        text: wizard.coachName
                        error: wizard.coachNameError
                        Accessible.name: coachNameFormField.label
                        onTextEdited: wizard.coachName = text
                    }
                }

                FormField {
                    id: pinFormField

                    Layout.fillWidth: true
                    label: qsTr("PIN")
                    mandatory: true
                    helpText: qsTr("4 to 8 digits. You will enter it at every start.")
                    error: pinInput.effectiveError

                    PinField {
                        id: pinInput

                        Layout.fillWidth: true
                        text: wizard.pin
                        error: wizard.pinError
                        Accessible.name: pinFormField.label
                        onTextEdited: wizard.pin = text
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
                        text: wizard.pinRepeat
                        error: wizard.pinRepeatError
                        Accessible.name: pinRepeatFormField.label
                        onTextEdited: wizard.pinRepeat = text
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                spacing: Theme.spacing.m

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.s

                    Repeater {
                        model: [
                            {
                                "label": qsTr("Club"),
                                "value": wizard.clubName
                            },
                            {
                                "label": qsTr("Team"),
                                "value": wizard.teamName
                            },
                            {
                                "label": qsTr("Category"),
                                "value": root.categoryName(wizard.category)
                            },
                            {
                                "label": qsTr("Season"),
                                "value": wizard.season
                            },
                            {
                                "label": qsTr("Coach"),
                                "value": wizard.coachName
                            }
                        ]

                        delegate: RowLayout {
                            id: summaryRow

                            required property var modelData

                            Layout.fillWidth: true
                            spacing: Theme.spacing.l

                            Label {
                                Layout.preferredWidth: Theme.fieldWidth / 2
                                text: summaryRow.modelData.label
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSize.body
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                text: summaryRow.modelData.value
                                textFormat: Text.PlainText
                                color: Theme.text
                                font.pixelSize: Theme.fontSize.body
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            SecondaryButton {
                visible: wizard.currentStep > 0
                text: qsTr("Back")
                onClicked: wizard.back()
            }

            Item {
                Layout.fillWidth: true
            }

            PrimaryButton {
                id: nextButton

                text: root.onLastStep ? qsTr("Create") : qsTr("Next")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("%1 (Enter)").arg(text)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.advance()
            }
        }
    }
}
