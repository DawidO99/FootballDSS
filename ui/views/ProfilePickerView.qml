pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Every start after the first: tiles of the active staff and the PIN of the one selected.
// Enter submits, Esc clears.
FocusScope {
    id: root

    // Exposed for tests (qml_smoke logs in with it).
    readonly property ProfilePickerViewModel viewModel: picker

    function submit() {
        if (picker.selectedId !== 0 && !picker.locked)
            picker.login(pinInput.text);
    }

    function clearPin() {
        pinInput.clear();
        root.focusPin();
    }

    function focusPin() {
        if (picker.selectedId !== 0 && !picker.locked)
            pinInput.forceActiveFocus(Qt.TabFocusReason);
    }

    // Tab can reach a tile below the fold; the keyboard user has to see where the focus went.
    function revealTile(tile: Item) {
        if (tile.y < tilesArea.contentY)
            tilesArea.contentY = tile.y;
        else if (tile.y + tile.height > tilesArea.contentY + tilesArea.height)
            tilesArea.contentY = tile.y + tile.height - tilesArea.height;
    }

    Keys.onReturnPressed: root.submit()
    Keys.onEnterPressed: root.submit()
    Keys.onEscapePressed: root.clearPin()
    Component.onCompleted: root.focusPin()

    ProfilePickerViewModel {
        id: picker

        onSelectionChanged: root.clearPin()
        onPinRejected: root.clearPin()
        onLoggedIn: AppState.refresh()
        onErrorOccurred: message => Toasts.error(message)
    }

    EmptyState {
        anchors.fill: parent
        visible: picker.loadError.length > 0
        iconName: "user"
        title: qsTr("The staff list could not be read")
        description: picker.loadError
        actionText: qsTr("Try again")
        onActionTriggered: picker.reload()
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.margins: Theme.spacing.xl
        width: Math.min(parent.width - 2 * Theme.spacing.xl, Theme.dialogWidthL + Theme.dialogWidthS)
        visible: picker.loadError.length === 0
        spacing: Theme.spacing.xl

        PageHeader {
            Layout.fillWidth: true
            title: AppState.clubName
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing.xl

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacing.m

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Who is using the app?")
                    color: Theme.text
                    font.pixelSize: Theme.fontSize.title
                    font.weight: Font.DemiBold
                }

                Flickable {
                    id: tilesArea

                    objectName: "tilesArea"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: tiles.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {}

                    Flow {
                        id: tiles

                        width: tilesArea.width
                        spacing: Theme.spacing.m

                        Repeater {
                            model: picker.staff

                            delegate: ProfileTile {
                                id: tile

                                required property double staffId
                                required name
                                required roleName
                                required initials

                                selected: picker.selectedId === staffId
                                onActiveFocusChanged: {
                                    if (activeFocus)
                                        root.revealTile(tile);
                                }
                                onClicked: {
                                    picker.select(tile.staffId);
                                    root.focusPin();
                                }
                            }
                        }
                    }
                }
            }

            Card {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: Theme.dialogWidthS
                title: picker.selectedId !== 0 ? picker.selectedName : qsTr("Choose your profile")

                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    visible: picker.selectedId === 0
                    text: qsTr("Pick your tile, then enter your PIN.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize.body
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: picker.selectedId !== 0
                    spacing: Theme.spacing.m

                    FormField {
                        id: pinFormField

                        Layout.fillWidth: true
                        label: qsTr("PIN")
                        error: picker.message

                        PinField {
                            id: pinInput

                            objectName: "pinInput"
                            Layout.fillWidth: true
                            enabled: !picker.locked
                            error: picker.message
                            Accessible.name: pinFormField.label
                            onEnabledChanged: root.focusPin()
                        }
                    }

                    PinPad {
                        Layout.alignment: Qt.AlignHCenter
                        enabled: !picker.locked
                        onDigitPressed: digit => pinInput.insert(pinInput.length, digit)
                        onBackspacePressed: pinInput.remove(pinInput.length - 1, pinInput.length)
                        onClearPressed: root.clearPin()
                    }

                    PrimaryButton {
                        objectName: "loginButton"
                        Layout.fillWidth: true
                        enabled: !picker.locked
                        touch: true
                        text: qsTr("Log in")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("%1 (Enter)").arg(text)
                        ToolTip.delay: Theme.tooltipDelay
                        onClicked: root.submit()
                    }
                }
            }
        }
    }
}
