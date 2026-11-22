pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// A short overview of the sections and their keyboard shortcuts (P19), opened by the "?" button of
// the top bar or F1. Every shortcut is also a visible control of its section (AGENTS section 6).
// API: open(), close()
AppDialog {
    id: root

    // [{ title, text, keys: [[sequence, action]] }]; the sequences are shown in the notation of
    // the platform.
    readonly property var sections: [
        {
            "title": qsTr("Everywhere"),
            "text": qsTr("The sidebar opens the sections; the top bar has the search, the next match, the language, the theme and your account."),
            "keys": [["Ctrl+K", qsTr("Search for a player")], ["F1", qsTr("This help")]]
        },
        {
            "title": qsTr("Dashboard"),
            "text": qsTr("The next match, unavailable players, load signals and recent form."),
            "keys": []
        },
        {
            "title": qsTr("Squad"),
            "text": qsTr("Players, their positions and availability. Enter or a double click opens the player card."),
            "keys": [["Ctrl+N", qsTr("Add player")], ["Esc", qsTr("Back from the player card")]]
        },
        {
            "title": qsTr("Calendar"),
            "text": qsTr("Matches and training sessions; a played match opens its report."),
            "keys": [["Ctrl+N", qsTr("Add a match or a session")], ["Ctrl+S", qsTr("Save the match report")]]
        },
        {
            "title": qsTr("Training"),
            "text": qsTr("Attendance, RPE and minutes. In the sheet a digit enters the RPE, Enter moves to the next row and Esc takes a cell back."),
            "keys": [["Ctrl+S", qsTr("Save")]]
        },
        {
            "title": qsTr("Pre-match"),
            "text": qsTr("The lineup on the tactics board, with its warnings and the lineup assistant."),
            "keys": [["Ctrl+S", qsTr("Save")]]
        },
        {
            "title": qsTr("Match Day"),
            "text": qsTr("The live match: clock, events, substitutions, notes and changes of shape."),
            "keys": [["G", qsTr("Goal")], ["O", qsTr("Opponent goal")], ["Y", qsTr("Yellow card")], ["R", qsTr("Red card")], ["S", qsTr("Substitution")], ["N", qsTr("Note")], ["Ctrl+Z", qsTr("Undo the last event")], ["F11", qsTr("Full screen")], ["Esc", qsTr("Cancel the current step")]]
        },
        {
            "title": qsTr("Load"),
            "text": qsTr("Load signals of every player: reasons to talk to the player, not injury predictions."),
            "keys": []
        },
        {
            "title": qsTr("Metrics"),
            "text": qsTr("Values of the custom metrics of your club."),
            "keys": [["Ctrl+S", qsTr("Save")]]
        },
        {
            "title": qsTr("Import"),
            "text": qsTr("Players, training sessions and matches from CSV or Excel files; every import can be undone."),
            "keys": [["Ctrl+O", qsTr("Choose file")]]
        },
        {
            "title": qsTr("Settings"),
            "text": qsTr("The team, match rules, load signals, staff and custom metrics."),
            "keys": []
        }
    ]

    objectName: "helpDialog"
    size: AppDialog.Large
    title: qsTr("Help")
    primaryText: qsTr("Close")
    secondaryText: ""
    onPrimaryClicked: root.close()

    Repeater {
        model: root.sections

        delegate: ColumnLayout {
            id: section

            required property var modelData

            Layout.fillWidth: true
            spacing: Theme.spacing.xs

            Label {
                Layout.fillWidth: true
                text: section.modelData.title
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: Theme.fontSize.body
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: section.modelData.text
                textFormat: Text.PlainText
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: section.modelData.keys

                delegate: RowLayout {
                    id: key

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Theme.spacing.m

                    // Only for its nativeText: Cmd instead of Ctrl on macOS.
                    Shortcut {
                        id: notation

                        sequence: key.modelData[0]
                        enabled: false
                    }

                    Label {
                        Layout.preferredWidth: Theme.helpKeyWidth
                        text: notation.nativeText
                        textFormat: Text.PlainText
                        color: Theme.accent
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: key.modelData[1]
                        textFormat: Text.PlainText
                        color: Theme.text
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
