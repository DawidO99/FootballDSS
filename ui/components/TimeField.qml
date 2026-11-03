import QtQuick

// Optional time of day. Outside value is always "HH:mm" or empty (AGENTS.md section 6); 18:30, 18.30,
// 1830 and 930 can be typed, and the text becomes HH:mm when the field is left. Text that is not a
// time yet shows an error after leaving and never changes the time silently.
// API:
//   time: string            input: "HH:mm" or ""; never assigned by the field
//   error: string           external message from the viewmodel
//   currentTime             read-only: the text as "HH:mm", empty when it is not a time
//   hasInvalidText          read-only: there is text and it is not a time, focused or not; a form
//                           keeps its Save off, because the viewmodel still has the last valid value
//   validationError, effectiveError   read-only, see TextInputField
//   timeEdited(string time) the user changed the text; empty until it is a time again
//   showTime()              shows `time` again, dropping the text typed since
// Edits stay inside the field, so a binding on `time` survives them. Keep a viewmodel in sync in
// timeEdited; whenever `time` changes to something else the field shows it.
// Set Accessible.name to the field label.
TextInputField {
    id: root

    property string time
    readonly property string currentTime: normalized(text)
    readonly property bool hasInvalidText: text.length > 0 && !acceptableInput

    signal timeEdited(string time)

    function normalized(typed) {
        const parts = /^(\d{1,2}?)[:.]?(\d{2})$/.exec(typed);
        if (!parts)
            return "";
        const hours = Number(parts[1]);
        const minutes = Number(parts[2]);
        if (hours > 23 || minutes > 59)
            return "";
        return String(hours).padStart(2, "0") + ":" + parts[2];
    }

    function showTime() {
        root.text = root.time;
    }

    //: What a time looks like: hours and minutes, as a hint in an empty field.
    placeholderText: qsTr("HH:mm")
    maximumLength: 5
    invalidMessage: qsTr("Enter the time as HH:mm, for example 18:30.")
    validator: RegularExpressionValidator {
        regularExpression: /([01]?\d|2[0-3])[:.]?[0-5]\d/
    }

    // A viewmodel may answer timeEdited right away, and textEdited comes before textChanged: both
    // handlers read the text, because currentTime still has the last one here.
    onTimeChanged: {
        if (root.normalized(root.text) !== root.time)
            root.showTime();
    }
    onTextEdited: root.timeEdited(root.normalized(root.text))
    onEditingFinished: {
        if (root.currentTime.length > 0 && root.text !== root.currentTime)
            root.text = root.currentTime;
    }
    Component.onCompleted: root.showTime()
}
