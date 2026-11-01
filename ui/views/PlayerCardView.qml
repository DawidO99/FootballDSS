pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// The card of one player, shown by SquadView. The Back button leads to the list; Esc is its shortcut.
Item {
    id: root

    // 0: no player.
    property double playerId: 0
    // False while a dialog of SquadView is open: Esc belongs to that dialog then.
    property bool shortcutsEnabled: true
    property var selectedEntry: ({})
    readonly property bool hasSelection: selectedEntry.entryId !== undefined
    readonly property bool canEditPlayer: AppState.can("EditSquad")
    readonly property alias viewModel: card
    readonly property alias availabilityModel: availability
    readonly property alias availabilityEditor: availabilityDialog
    readonly property alias returnEditor: returnDialog

    signal backRequested
    signal editRequested(double playerId)
    // After a change of availability, which changes the status in the list too.
    signal squadChanged

    function reload() {
        card.reload();
        availability.reload();
    }

    function focusFirst() {
        overviewPage.scrollTo(0);
        backButton.forceActiveFocus(Qt.TabFocusReason);
    }

    function syncSelection() {
        root.selectedEntry = availability.entries.rowAt(entriesTable.currentIndex);
    }

    function openAddAvailability() {
        availabilityDialog.openForAdding();
    }

    function openEditAvailability() {
        if (root.hasSelection && availability.canEdit)
            availabilityDialog.openForEditing(root.selectedEntry);
    }

    function openMarkReturned() {
        if (!root.hasSelection)
            return;
        availability.clearErrors();
        returnDialog.entryId = root.selectedEntry.entryId;
        returnDialog.minimumDate = root.selectedEntry.fromDate;
        returnDialog.suggestedDate = availability.defaultReturnDate(root.selectedEntry.entryId);
        // The same date as last time changes nothing in the field, so an edit left there would stay.
        returnInput.showDate();
        returnDialog.open();
        returnInput.forceActiveFocus(Qt.TabFocusReason);
    }

    // The table keeps these for itself while it has the focus.
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
            overviewPage.scrollByPage(event.key === Qt.Key_PageDown ? 1 : -1);
            event.accepted = true;
        }
    }

    PlayerCardViewModel {
        id: card

        playerId: root.playerId
    }

    AvailabilityViewModel {
        id: availability

        playerId: root.playerId
        onErrorOccurred: message => Toasts.error(message)
        onEntriesChanged: {
            // The list was read anew: the same entry stays selected.
            entriesTable.currentIndex = availability.entries.rowOf(root.selectedEntry.entryId ?? 0);
            root.syncSelection();
        }
        onAvailabilityChanged: {
            card.reload();
            root.squadChanged();
        }
    }

    Shortcut {
        id: backShortcut

        objectName: "backShortcut"
        sequence: "Esc"
        enabled: root.visible && root.shortcutsEnabled && !availabilityDialog.visible && !returnDialog.visible
        onActivated: root.backRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.xl
        spacing: Theme.spacing.l

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.m

            SecondaryButton {
                id: backButton

                objectName: "backButton"
                text: qsTr("← Squad")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back to the list (%1)").arg(backShortcut.nativeText)
                ToolTip.delay: Theme.tooltipDelay
                onClicked: root.backRequested()
            }

            Label {
                visible: card.number !== undefined
                text: String(card.number ?? "")
                color: Theme.accent
                font.pixelSize: Theme.fontSize.h1
                font.weight: Font.DemiBold
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: card.name
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.h1
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: card.age !== undefined ? qsTr("%1, age %2").arg(card.positionName).arg(card.age) : card.positionName
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    elide: Text.ElideRight
                }
            }

            PlayerStatusBadge {
                visible: card.statusKey.length > 0
                statusKey: card.statusKey
                text: card.statusText
            }

            SecondaryButton {
                visible: root.canEditPlayer
                text: qsTr("Edit player")
                onClicked: root.editRequested(root.playerId)
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: card.loadError.length > 0
            iconName: "user"
            title: qsTr("This player could not be read")
            description: card.loadError
            actionText: qsTr("Try again")
            onActionTriggered: root.reload()
        }

        AppTabs {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: card.loadError.length === 0
            titles: [qsTr("Overview"), qsTr("Load"), qsTr("Matches"), qsTr("Notes")]

            // Long notes and descriptions scroll the page. The cards fill it at least, and stay at the
            // top of a higher one, so the table and its buttons do not move away from the tabs.
            FocusFlickable {
                id: overviewPage

                objectName: "overviewPage"
                contentHeight: Math.max(height, overviewRow.implicitHeight)

                RowLayout {
                    id: overviewRow

                    width: overviewPage.width
                    spacing: Theme.spacing.l

                    Card {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: Math.max(overviewPage.height, implicitHeight)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        title: qsTr("Player data")

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacing.s

                            Repeater {
                                model: [
                                    {
                                        "key": "position",
                                        "label": qsTr("Position"),
                                        "value": card.positionCode.length > 0 ? card.positionCode + " - " + card.positionName : ""
                                    },
                                    {
                                        "key": "secondaryPositions",
                                        "label": qsTr("Secondary positions"),
                                        "value": card.secondaryPositions
                                    },
                                    {
                                        "key": "birthYear",
                                        "label": qsTr("Birth year"),
                                        "value": String(card.birthYear ?? "")
                                    },
                                    {
                                        "key": "foot",
                                        "label": qsTr("Preferred foot"),
                                        "value": card.footName
                                    },
                                    {
                                        "key": "notes",
                                        "label": qsTr("Notes"),
                                        "value": card.notes
                                    }
                                ]

                                delegate: RowLayout {
                                    id: dataRow

                                    required property var modelData

                                    Layout.fillWidth: true
                                    spacing: Theme.spacing.m

                                    Label {
                                        Layout.preferredWidth: Theme.fieldWidth / 2
                                        Layout.alignment: Qt.AlignTop
                                        text: dataRow.modelData.label
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontSize.body
                                        wrapMode: Text.WordWrap
                                    }

                                    // Long notes wrap inside the card, in the middle of a long word too.
                                    Label {
                                        objectName: "playerData_" + dataRow.modelData.key
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 0
                                        text: dataRow.modelData.value.length > 0 ? dataRow.modelData.value : qsTr("Not set")
                                        textFormat: Text.PlainText
                                        color: Theme.text
                                        font.pixelSize: Theme.fontSize.body
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }

                    Card {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredHeight: Math.max(overviewPage.height, implicitHeight)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 2
                        title: qsTr("Availability")

                        DataTable {
                            id: entriesTable

                            objectName: "entriesTable"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Accessible.name: qsTr("Availability")
                            model: availability.entries
                            columns: {
                                const columns = [
                                    {
                                        "role": "typeName",
                                        "title": qsTr("Type"),
                                        "width": 0.2,
                                        "sortable": false
                                    },
                                    {
                                        "role": "fromText",
                                        "title": qsTr("From"),
                                        "width": 0.2,
                                        "sortable": false
                                    },
                                    {
                                        "role": "expectedText",
                                        "title": qsTr("Expected"),
                                        "width": 0.2,
                                        "sortable": false
                                    },
                                    {
                                        "role": "actualText",
                                        "title": qsTr("Returned"),
                                        "width": 0.2,
                                        "sortable": false
                                    }
                                ];
                                if (availability.canViewDetails)
                                    columns.push({
                                        "role": "description",
                                        "title": qsTr("Description"),
                                        "width": 0.4,
                                        "sortable": false
                                    });
                                return columns;
                            }
                            emptyState: entriesEmptyState
                            onCurrentIndexChanged: root.syncSelection()
                            onActivated: root.openEditAvailability()
                        }

                        // The table shows one line of a description; here it can be read to the end.
                        Label {
                            objectName: "selectedDescription"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 0
                            visible: availability.canViewDetails && text.length > 0
                            text: root.selectedEntry.description ?? ""
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: Theme.fontSize.body
                            wrapMode: Text.Wrap
                        }

                        Component {
                            id: entriesEmptyState

                            EmptyState {
                                iconName: "calendar"
                                title: availability.loadError.length > 0 ? qsTr("The availability could not be read") : qsTr("No availability entries")
                                description: availability.loadError.length > 0 ? availability.loadError : qsTr("Injuries, illnesses, suspensions and absences of this player will appear here.")
                                actionText: availability.loadError.length > 0 ? qsTr("Try again") : availability.canEdit && !card.archived ? qsTr("Add availability") : ""
                                onActionTriggered: {
                                    if (availability.loadError.length > 0)
                                        availability.reload();
                                    else
                                        root.openAddAvailability();
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: availability.canEdit
                            spacing: Theme.spacing.m

                            PrimaryButton {
                                visible: !card.archived
                                text: qsTr("Add availability")
                                onClicked: root.openAddAvailability()
                            }

                            SecondaryButton {
                                enabled: root.hasSelection
                                text: qsTr("Edit")
                                onClicked: root.openEditAvailability()
                            }

                            SecondaryButton {
                                enabled: root.hasSelection && root.selectedEntry.open
                                text: qsTr("Mark as returned")
                                onClicked: root.openMarkReturned()
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            EmptyState {
                iconName: "load"
                title: qsTr("Load")
                description: qsTr("The training load of this player will appear here.")
            }

            EmptyState {
                iconName: "matchday"
                title: qsTr("Matches")
                description: qsTr("The matches, minutes and ratings of this player will appear here.")
            }

            EmptyState {
                iconName: "edit"
                title: qsTr("Notes")
                description: qsTr("Dated notes about this player will appear here.")
            }
        }
    }

    AvailabilityDialog {
        id: availabilityDialog

        viewModel: availability
    }

    AppDialog {
        id: returnDialog

        property double entryId: 0
        property string minimumDate
        property string suggestedDate

        size: AppDialog.Small
        title: qsTr("Mark as returned")
        primaryText: qsTr("Mark as returned")
        primaryEnabled: returnInput.validationError.length === 0
        onPrimaryClicked: {
            if (availability.markReturned(returnDialog.entryId, returnInput.currentDate))
                returnDialog.close();
        }

        FormField {
            id: returnField

            Layout.fillWidth: true
            label: qsTr("Available again from")
            mandatory: true
            helpText: qsTr("The first day the player is available.")
            error: returnInput.effectiveError

            DateField {
                id: returnInput

                Layout.fillWidth: true
                date: returnDialog.suggestedDate
                minimumDate: returnDialog.minimumDate
                error: availability.actualToError
                Accessible.name: returnField.label
            }
        }
    }
}
