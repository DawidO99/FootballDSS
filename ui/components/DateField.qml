pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FootballDSS

// Date input with a text field (formatted in the app language: PL dd.MM.yyyy, EN dd/MM/yyyy,
// always 4-digit year) and a calendar popup (MonthGrid + DayOfWeekRow).
// Outside value is always an ISO string "yyyy-MM-dd" (empty = none; AGENTS.md section 6).
// Invalid or out-of-range text sets validationError and never changes the date silently. Changing
// the limits validates the text again; the calendar cannot leave them.
// API:
//   date: string            input: ISO "yyyy-MM-dd" or ""; never assigned by the field
//   minimumDate: string     ISO lower bound, default 1900-01-01
//   maximumDate: string     ISO upper bound, default 2100-12-31
//                           Limits never go beyond those defaults (core isSupportedDate()); empty
//                           means the default.
//   optional: bool          empty text is valid ("")
//   error: string           external message from viewmodel
//   currentDate             read-only: `date`, or the user's last valid edit since `date` changed
//   displayText             read-only: the text in the field
//   displayFormat           read-only ("dd.MM.yyyy" or "dd/MM/yyyy")
//   validationError, effectiveError   read-only
//   dateEdited(string date) user typed or picked a valid date
//   openCalendar()          opens the calendar popup (also Alt+Down)
// Edits stay inside the field, so a binding on `date` survives them. Keep a viewmodel in sync in
// dateEdited; whenever `date` changes the field shows it again.
FocusScope {
    id: root

    property string date
    property string minimumDate: internal.supportedMinimum
    property string maximumDate: internal.supportedMaximum
    property bool optional: false
    property string error
    readonly property string currentDate: internal.currentDate
    readonly property string displayText: input.text
    readonly property bool polish: LanguageManager.language === "pl"
    readonly property string displayFormat: polish ? "dd.MM.yyyy" : "dd/MM/yyyy"
    readonly property string localeName: polish ? "pl_PL" : "en_GB"
    readonly property string validationError: internal.validationError
    readonly property string effectiveError: validationError.length > 0 ? validationError : error

    signal dateEdited(string date)

    function openCalendar() {
        if (!root.enabled)
            return;
        const today = new Date();
        const fallback = root.toIso(today.getFullYear(), today.getMonth() + 1, today.getDate());
        const initial = root.parseIso(root.clampToRange(root.parseIso(internal.currentDate) ? internal.currentDate : fallback));
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
        return iso < internal.lowerBound || iso > internal.upperBound;
    }

    function clampToRange(iso) {
        return iso < internal.lowerBound ? internal.lowerBound : iso > internal.upperBound ? internal.upperBound : iso;
    }

    function rangeErrorFor(iso) {
        if (iso < internal.lowerBound)
            return qsTr("Enter a date on or after %1.").arg(root.formatIso(internal.lowerBound));
        if (iso > internal.upperBound)
            return qsTr("Enter a date on or before %1.").arg(root.formatIso(internal.upperBound));
        return "";
    }

    function applyIso(iso) {
        if (iso === internal.currentDate)
            return;
        internal.currentDate = iso;
        root.dateEdited(iso);
    }

    // The outside date replaces the field's state.
    function showDate() {
        internal.currentDate = root.date;
        const formatted = root.formatIso(root.date);
        if (input.text !== formatted)
            input.text = formatted;
        internal.validationError = formatted.length > 0 ? root.rangeErrorFor(root.date) : "";
    }

    // The limits changed. An untouched empty field stays valid; text that was out of range and now
    // fits is the user's edit, so it is committed like any other.
    function revalidate() {
        if (internal.completed && input.text.trim().length > 0)
            root.commitText(input.text);
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

    // Year and 0-based month `delta` months after the calendar's current month.
    function monthAfter(delta) {
        const total = internal.viewYear * 12 + internal.viewMonth + delta;
        return {
            "year": Math.floor(total / 12),
            "month": total % 12
        };
    }

    function canShiftMonth(delta) {
        const target = root.monthAfter(delta);
        const firstDay = root.toIso(target.year, target.month + 1, 1);
        const lastDay = root.toIso(target.year, target.month + 1, root.daysInMonth(target.year, target.month + 1));
        return lastDay >= internal.lowerBound && firstDay <= internal.upperBound;
    }

    function shiftMonth(delta) {
        if (!root.canShiftMonth(delta))
            return;
        const target = root.monthAfter(delta);
        const current = root.parseIso(internal.cursorIso);
        const day = current ? Math.min(current.day, root.daysInMonth(target.year, target.month + 1)) : 1;
        internal.viewYear = target.year;
        internal.viewMonth = target.month;
        internal.cursorIso = root.clampToRange(root.toIso(target.year, target.month + 1, day));
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
        const target = root.parseIso(root.clampToRange(root.toIso(year, month, day)));
        internal.viewYear = target.year;
        internal.viewMonth = target.month - 1;
        internal.cursorIso = root.toIso(target.year, target.month, target.day);
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
        if (internal.completed)
            root.showDate();
    }
    onDisplayFormatChanged: {
        if (internal.validationError.length === 0)
            input.text = root.formatIso(internal.currentDate);
        else
            root.commitText(input.text);
    }
    Component.onCompleted: {
        internal.completed = true;
        root.showDate();
    }

    QtObject {
        id: internal

        readonly property string supportedMinimum: "1900-01-01"
        readonly property string supportedMaximum: "2100-12-31"
        readonly property string lowerBound: root.minimumDate > supportedMinimum ? root.minimumDate : supportedMinimum
        readonly property string upperBound: root.maximumDate.length > 0 && root.maximumDate < supportedMaximum ? root.maximumDate : supportedMaximum
        property bool completed: false
        property string currentDate
        property string validationError
        property int viewYear: 2026
        property int viewMonth: 10
        property string cursorIso: "2026-11-01"

        // On the bounds, not on the limit properties: those handlers can run before these update.
        onLowerBoundChanged: root.revalidate()
        onUpperBoundChanged: root.revalidate()
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
                text = root.formatIso(internal.currentDate);
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
        // Seven day columns of at least hitMin, even if the field token shrinks.
        width: Math.max(Theme.fieldWidth, 7 * Theme.hitMin + 2 * padding)
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
                        enabled: root.canShiftMonth(-1)
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
                        enabled: root.canShiftMonth(1)
                        onClicked: root.shiftMonth(1)
                    }
                }

                DayOfWeekRow {
                    Layout.fillWidth: true
                    spacing: 0
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

                    objectName: "calendarMonthGrid"
                    Layout.fillWidth: true
                    implicitHeight: 6 * Theme.hitMin
                    spacing: 0
                    month: internal.viewMonth
                    year: internal.viewYear
                    locale: Qt.locale(root.localeName)

                    delegate: AbstractButton {
                        id: dayCell

                        required property var model
                        readonly property string cellIso: root.toIso(model.year, model.month + 1, model.day)
                        readonly property bool inMonth: model.month === grid.month
                        readonly property bool outOfRange: root.isOutOfRange(cellIso)
                        readonly property bool selected: cellIso === internal.currentDate
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
