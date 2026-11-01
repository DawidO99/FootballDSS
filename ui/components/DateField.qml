pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Date input with a text field (formatted in the app language: PL dd.MM.yyyy, EN dd/MM/yyyy,
// always 4-digit year) and a calendar popup (MonthGrid + DayOfWeekRow).
// Outside value is always an ISO string "yyyy-MM-dd" (empty = none; AGENTS.md section 6).
// Invalid or out-of-range text sets validationError and never changes `date` silently.
// API:
//   date: string            ISO "yyyy-MM-dd" or ""
//   minimumDate: string     optional ISO lower bound "yyyy-MM-dd"
//   maximumDate: string     optional ISO upper bound "yyyy-MM-dd"
//   optional: bool          empty text is valid ("")
//   error: string           external message from viewmodel
//   displayFormat           read-only ("dd.MM.yyyy" or "dd/MM/yyyy")
//   validationError, effectiveError   read-only
//   dateEdited(string date) user typed or picked a valid date (`date` is already updated)
//   openCalendar()          opens the calendar popup (also Alt+Down)
FocusScope {
    id: root

    property string date
    property string minimumDate
    property string maximumDate
    property bool optional: false
    property string error
    readonly property bool polish: LanguageManager.language === "pl"
    readonly property string displayFormat: polish ? "dd.MM.yyyy" : "dd/MM/yyyy"
    readonly property string localeName: polish ? "pl_PL" : "en_GB"
    readonly property string validationError: internal.validationError
    readonly property string effectiveError: validationError.length > 0 ? validationError : error

    signal dateEdited(string date)

    function openCalendar() {
        if (!root.enabled)
            return;
        const initial = root.parseIso(root.date) ?? root.parseIso(root.minimumDate) ?? {
            "year": 2026,
            "month": 11,
            "day": 1
        };
        internal.viewYear = initial.year;
        internal.viewMonth = initial.month - 1;
        internal.cursorIso = root.toIso(initial.year, initial.month, initial.day);
        calendarPopup.open();
        calendarGrid.forceActiveFocus(Qt.PopupFocusReason);
    }

    function pad2(n) {
        return n < 10 ? "0" + String(n) : String(n);
    }

    function daysInMonth(year, month) {
        if (month === 2) {
            const leap = (year % 4 === 0 && year % 100 !== 0) || (year % 400 === 0);
            return leap ? 29 : 28;
        }
        if (month === 4 || month === 6 || month === 9 || month === 11)
            return 30;
        return 31;
    }

    function isValidYmd(year, month, day) {
        return year >= 1000 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= root.daysInMonth(year, month);
    }

    function toIso(year, month, day) {
        return String(year) + "-" + root.pad2(month) + "-" + root.pad2(day);
    }

    function parseIso(iso) {
        const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso);
        if (!match)
            return null;
        const year = Number(match[1]);
        const month = Number(match[2]);
        const day = Number(match[3]);
        if (!root.isValidYmd(year, month, day))
            return null;
        return {
            "year": year,
            "month": month,
            "day": day
        };
    }

    function formatIso(iso) {
        const parts = root.parseIso(iso);
        if (!parts)
            return "";
        const sep = root.polish ? "." : "/";
        return root.pad2(parts.day) + sep + root.pad2(parts.month) + sep + String(parts.year);
    }

    function parseDisplay(text) {
        const trimmed = text.trim();
        const pattern = root.polish ? /^(\d{2})\.(\d{2})\.(\d{4})$/ : /^(\d{2})\/(\d{2})\/(\d{4})$/;
        const match = pattern.exec(trimmed);
        if (!match)
            return null;
        const day = Number(match[1]);
        const month = Number(match[2]);
        const year = Number(match[3]);
        if (!root.isValidYmd(year, month, day))
            return null;
        return root.toIso(year, month, day);
    }

    function isOutOfRange(iso) {
        if (root.minimumDate.length > 0 && iso < root.minimumDate)
            return true;
        if (root.maximumDate.length > 0 && iso > root.maximumDate)
            return true;
        return false;
    }

    function rangeErrorFor(iso) {
        if (root.minimumDate.length > 0 && iso < root.minimumDate)
            return qsTr("Enter a date on or after %1.").arg(root.formatIso(root.minimumDate));
        if (root.maximumDate.length > 0 && iso > root.maximumDate)
            return qsTr("Enter a date on or before %1.").arg(root.formatIso(root.maximumDate));
        return "";
    }

    function applyIso(iso) {
        if (iso === root.date)
            return;
        root.date = iso;
        root.dateEdited(iso);
    }

    function commitText(text) {
        const trimmed = text.trim();
        if (trimmed.length === 0) {
            if (root.optional) {
                internal.validationError = "";
                root.applyIso("");
            } else {
                internal.validationError = qsTr("Enter a date (%1).").arg(root.displayFormat);
            }
            return;
        }
        const iso = root.parseDisplay(trimmed);
        if (iso === null) {
            internal.validationError = qsTr("Use format %1.").arg(root.displayFormat);
            return;
        }
        const rangeError = root.rangeErrorFor(iso);
        if (rangeError.length > 0) {
            internal.validationError = rangeError;
            return;
        }
        internal.validationError = "";
        root.applyIso(iso);
    }

    function shiftMonth(delta) {
        let month = internal.viewMonth + delta;
        let year = internal.viewYear;
        while (month < 0) {
            month += 12;
            year -= 1;
        }
        while (month > 11) {
            month -= 12;
            year += 1;
        }
        internal.viewYear = year;
        internal.viewMonth = month;
        const current = root.parseIso(internal.cursorIso);
        const day = current ? Math.min(current.day, root.daysInMonth(year, month + 1)) : 1;
        internal.cursorIso = root.toIso(year, month + 1, day);
    }

    function moveCursorDays(delta) {
        const parts = root.parseIso(internal.cursorIso) ?? {
            "year": internal.viewYear,
            "month": internal.viewMonth + 1,
            "day": 1
        };
        let year = parts.year;
        let month = parts.month;
        let day = parts.day + delta;
        while (day < 1) {
            month -= 1;
            if (month < 1) {
                month = 12;
                year -= 1;
            }
            day += root.daysInMonth(year, month);
        }
        while (day > root.daysInMonth(year, month)) {
            day -= root.daysInMonth(year, month);
            month += 1;
            if (month > 12) {
                month = 1;
                year += 1;
            }
        }
        internal.viewYear = year;
        internal.viewMonth = month - 1;
        internal.cursorIso = root.toIso(year, month, day);
    }

    function pickIso(iso) {
        if (root.isOutOfRange(iso))
            return;
        internal.validationError = "";
        input.text = root.formatIso(iso);
        root.applyIso(iso);
        calendarPopup.close();
        input.forceActiveFocus(Qt.PopupFocusReason);
    }

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.controlHeight
    activeFocusOnTab: true
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    onDateChanged: {
        const formatted = root.formatIso(root.date);
        if (input.text !== formatted) {
            input.text = formatted;
            internal.validationError = "";
        }
    }
    onDisplayFormatChanged: {
        if (internal.validationError.length === 0)
            input.text = root.formatIso(root.date);
        else
            root.commitText(input.text);
    }
    Component.onCompleted: input.text = root.formatIso(root.date)

    QtObject {
        id: internal

        property string validationError
        property int viewYear: 2026
        property int viewMonth: 10
        property string cursorIso: "2026-11-01"
    }

    TextField {
        id: input

        anchors.fill: parent
        focus: true
        leftPadding: Theme.spacing.m
        rightPadding: calendarButton.width + Theme.spacing.xs
        placeholderText: root.displayFormat
        placeholderTextColor: Theme.textMuted
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        font.pixelSize: Theme.fontSize.body
        hoverEnabled: true
        Accessible.role: Accessible.EditableText
        Accessible.name: root.Accessible.name.length > 0 ? root.Accessible.name : root.displayFormat
        onTextEdited: root.commitText(text)
        onEditingFinished: {
            if (root.validationError.length === 0)
                text = root.formatIso(root.date);
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) {
                root.openCalendar();
                event.accepted = true;
            }
        }

        background: FieldBackground {
            active: input.activeFocus || calendarPopup.visible
            hasError: root.effectiveError.length > 0
            hovered: input.hovered
        }

        IconButton {
            id: calendarButton

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            iconName: "calendar"
            iconColor: Theme.textMuted
            text: qsTr("Open calendar")
            focusPolicy: Qt.NoFocus
            onClicked: {
                if (calendarPopup.visible)
                    calendarPopup.close();
                else
                    root.openCalendar();
            }
        }
    }

    Popup {
        id: calendarPopup

        y: root.height + Theme.spacing.xs
        width: Theme.fieldWidth
        padding: Theme.spacing.s
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius.m
            border.width: Theme.borderWidth
            border.color: Theme.border
        }

        contentItem: FocusScope {
            id: calendarGrid

            implicitWidth: calendarColumn.implicitWidth
            implicitHeight: calendarColumn.implicitHeight
            focus: true
            Keys.onLeftPressed: root.moveCursorDays(-1)
            Keys.onRightPressed: root.moveCursorDays(1)
            Keys.onUpPressed: root.moveCursorDays(-7)
            Keys.onDownPressed: root.moveCursorDays(7)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp) {
                    root.shiftMonth(-1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_PageDown) {
                    root.shiftMonth(1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    root.pickIso(internal.cursorIso);
                    event.accepted = true;
                }
            }

            ColumnLayout {
                id: calendarColumn

                anchors.fill: parent
                spacing: Theme.spacing.xs

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.xs

                    IconButton {
                        iconName: "chevronLeft"
                        text: qsTr("Previous month")
                        focusPolicy: Qt.NoFocus
                        onClicked: root.shiftMonth(-1)
                    }

                    Label {
                        Layout.fillWidth: true
                        text: Qt.locale(root.localeName).standaloneMonthName(internal.viewMonth, Locale.LongFormat) + " " + String(internal.viewYear)
                        color: Theme.text
                        font.pixelSize: Theme.fontSize.body
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    IconButton {
                        iconName: "chevronRight"
                        text: qsTr("Next month")
                        focusPolicy: Qt.NoFocus
                        onClicked: root.shiftMonth(1)
                    }
                }

                DayOfWeekRow {
                    Layout.fillWidth: true
                    locale: Qt.locale(root.localeName)

                    delegate: Label {
                        required property string narrowName

                        text: narrowName
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSize.caption
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                MonthGrid {
                    id: grid

                    Layout.fillWidth: true
                    implicitHeight: 6 * Theme.hitMin
                    month: internal.viewMonth
                    year: internal.viewYear
                    locale: Qt.locale(root.localeName)

                    delegate: AbstractButton {
                        id: dayCell

                        required property var model
                        readonly property string cellIso: root.toIso(model.year, model.month + 1, model.day)
                        readonly property bool inMonth: model.month === grid.month
                        readonly property bool outOfRange: root.isOutOfRange(cellIso)
                        readonly property bool selected: cellIso === root.date
                        readonly property bool cursorHere: cellIso === internal.cursorIso

                        implicitWidth: Theme.hitMin
                        implicitHeight: Theme.hitMin
                        enabled: !outOfRange
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        opacity: !inMonth || outOfRange ? Theme.disabledOpacity : 1.0
                        text: String(model.day)
                        Accessible.name: cellIso
                        onClicked: {
                            internal.cursorIso = cellIso;
                            root.pickIso(cellIso);
                        }

                        background: Rectangle {
                            radius: Theme.radius.s
                            color: dayCell.selected ? Theme.accent : dayCell.hovered ? Theme.rowHover : "transparent"
                            border.width: dayCell.cursorHere && calendarGrid.activeFocus ? Theme.focusRingWidth : 0
                            border.color: Theme.accent
                        }

                        contentItem: Label {
                            text: dayCell.text
                            color: dayCell.selected ? Theme.accentText : Theme.text
                            font.pixelSize: Theme.fontSize.caption
                            font.weight: dayCell.selected ? Font.DemiBold : Font.Normal
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
