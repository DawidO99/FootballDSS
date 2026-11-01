pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Language buttons and the theme toggle: in the top bar and in the corner of the screens before a
// login. A click leaves the keyboard focus where it was, so a PIN being typed goes on; Tab reaches
// them.
RowLayout {
    id: root

    spacing: Theme.spacing.l

    Row {
        spacing: Theme.spacing.xs

        Repeater {
            model: LanguageManager.availableLanguages

            delegate: AbstractButton {
                id: languageButton

                required property string modelData
                readonly property bool selected: LanguageManager.language === modelData

                objectName: "language_" + modelData
                implicitWidth: Math.max(Theme.hitMin + Theme.spacing.s, implicitContentWidth)
                implicitHeight: Theme.hitMin
                focusPolicy: Qt.TabFocus
                text: modelData.toUpperCase()
                hoverEnabled: true
                Accessible.name: qsTr("Interface language: %1").arg(Qt.locale(modelData).nativeLanguageName)
                Keys.onReturnPressed: click()
                Keys.onEnterPressed: click()
                onClicked: LanguageManager.setLanguage(modelData)

                background: Rectangle {
                    radius: Theme.radius.s
                    color: languageButton.selected ? Theme.accent : languageButton.hovered ? Theme.surfaceAlt : "transparent"
                    border.width: languageButton.visualFocus ? 2 : 1
                    border.color: languageButton.visualFocus ? Theme.text : languageButton.selected ? Theme.accent : Theme.border
                }

                contentItem: Label {
                    text: languageButton.text
                    color: languageButton.selected ? Theme.accentText : Theme.textMuted
                    font.pixelSize: Theme.fontSize.caption
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    AbstractButton {
        id: themeToggle

        objectName: "themeToggle"
        implicitWidth: Theme.hitMin + Theme.spacing.xs
        implicitHeight: Theme.hitMin + Theme.spacing.xs
        focusPolicy: Qt.TabFocus
        hoverEnabled: true
        text: AppSettings.darkTheme ? qsTr("Switch to light theme") : qsTr("Switch to dark theme")
        Accessible.name: text
        ToolTip.visible: hovered
        ToolTip.text: text
        ToolTip.delay: Theme.tooltipDelay
        Keys.onReturnPressed: click()
        Keys.onEnterPressed: click()
        onClicked: AppSettings.darkTheme = !AppSettings.darkTheme

        background: Rectangle {
            radius: Theme.radius.m
            color: themeToggle.hovered ? Theme.surfaceAlt : "transparent"
            border.width: themeToggle.visualFocus ? 2 : 0
            border.color: Theme.accent
        }

        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: AppSettings.darkTheme ? "sun" : "moon"
                color: Theme.text
            }
        }
    }
}
