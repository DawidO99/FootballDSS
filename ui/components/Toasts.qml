pragma Singleton

import QtQuick

// Global notification state rendered by the single ToastHost in Main.qml.
// Only one toast is active at a time; a new toast replaces the previous one.
// API:
//   Toasts.show(text, undoText, undoCallback)   action toast with an Undo button (10 s)
//   Toasts.info(text)                           informational toast (10 s)
//   Toasts.error(text)                          error toast (10 s)
//   Toasts.undo()                               invokes the undo callback and dismisses the toast
//   Toasts.dismiss()                            hides the current toast without undoing
QtObject {
    id: root

    readonly property bool active: internal.active
    readonly property string text: internal.text
    readonly property string undoText: internal.undoText
    readonly property int kind: internal.kind
    readonly property int serial: internal.serial

    function show(message, undoLabel, undoCallback) {
        internal.text = message;
        internal.undoText = undoLabel ?? "";
        internal.undoCallback = undoCallback ?? null;
        internal.kind = StatusBadge.Info;
        internal.serial += 1;
        internal.active = true;
    }

    function info(message) {
        root.show(message, "", null);
    }

    function error(message) {
        internal.text = message;
        internal.undoText = "";
        internal.undoCallback = null;
        internal.kind = StatusBadge.Danger;
        internal.serial += 1;
        internal.active = true;
    }

    function undo() {
        if (!internal.active)
            return;
        const callback = internal.undoCallback;
        root.dismiss();
        if (typeof callback === "function") {
            try {
                callback();
            } catch (err) {
                // Target view was destroyed before Undo was pressed; skip silently.
            }
        }
    }

    function dismiss() {
        internal.active = false;
        internal.text = "";
        internal.undoText = "";
        internal.undoCallback = null;
    }

    readonly property QtObject stateHolder: QtObject {
        id: internal

        property bool active: false
        property string text
        property string undoText
        property int kind: StatusBadge.Info
        property int serial: 0
        property var undoCallback: null
    }
}

