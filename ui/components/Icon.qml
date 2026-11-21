import QtQuick
import QtQuick.Shapes

// Line icon drawn on a Theme.iconGrid square; color follows the theme like text does.
Item {
    id: root

    property string name
    property color color: Theme.text
    property real size: Theme.iconSize.m

    // Workaround for a Qt scene graph bug (fix: icons respect clipping in lists, docs/BACKLOG.md).
    // What: four one-unit segments added to every path, far outside any window (in units of the
    // grid, scaled with the icon, so still off screen for any size); they are never seen.
    // Why: the scene graph does not clip a Shape that lies wholly outside the clip of an ancestor
    // while that ancestor is drawn: the icons of the cut last row of a scrolled list were drawn
    // below the list, with the CurveRenderer and the GeometryRenderer alike, and a clip on the icon
    // does not help. A Shape that crosses the edge of the clip is clipped. With these segments the
    // node always crosses it, so it is clipped like any item and the icon looks exactly as before.
    // A layer clips too, but resamples a texture at a fractional position: blurred or thicker icons.
    // Guarded by: tests/qml_components/tst_iconclip.qml (no pixel of the cut row below the list;
    // an icon in view is the same, pixel for pixel, as a plain Shape) and the clip check of the
    // squad list in qml_smoke.
    // Remove when: a Qt version passes tst_iconclip.qml without these segments (set boundsGuide to
    // "" and run it).
    readonly property string boundsGuide: " M12 -100000v1 M12 100000v1 M-100000 12h1 M100000 12h1"
    readonly property var paths: ({
            "dashboard": "M3 3h7v9H3z M14 3h7v5h-7z M14 12h7v9h-7z M3 16h7v5H3z",
            "squad": "M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M2 21v-1a6 6 0 0 1 6-6h2a6 6 0 0 1 6 6v1 "
                     + "M16 3.5a4 4 0 0 1 0 7.5 M22 21v-1a6 6 0 0 0-4-5.6",
            "calendar": "M4 5h16a1 1 0 0 1 1 1v14a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z "
                        + "M3 10h18 M8 3v4 M16 3v4",
            "training": "M6 7v10 M18 7v10 M3 9.5v5 M21 9.5v5 M6 12h12",
            "prematch": "M3 3l5 5 M8 3l-5 5 M18 21a3 3 0 1 0 0-6 3 3 0 0 0 0 6z "
                        + "M6 14c1-4.5 4-7.5 9-8.5 M12.5 3.5l2.5 2-2 2.7",
            "matchday": "M12 21a8 8 0 1 0 0-16 8 8 0 0 0 0 16z M12 9v4l2.5 2.5 M10 2h4 M12 2v3",
            "load": "M3 12h4l3-8 4 16 3-8h4",
            "import": "M12 3v12 M7 10l5 5 5-5 M4 17v3a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-3",
            "settings": "M4 6h9 M17 6h3 M15 8a2 2 0 1 0 0-4 2 2 0 0 0 0 4z "
                        + "M4 12h3 M11 12h9 M9 14a2 2 0 1 0 0-4 2 2 0 0 0 0 4z "
                        + "M4 18h11 M19 18h1 M17 20a2 2 0 1 0 0-4 2 2 0 0 0 0 4z",
            "search": "M11 18a7 7 0 1 0 0-14 7 7 0 0 0 0 14z M16 16l5 5",
            "sun": "M12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M12 2v2 M12 20v2 M2 12h2 M20 12h2 "
                   + "M4.9 4.9l1.4 1.4 M17.7 17.7l1.4 1.4 M4.9 19.1l1.4-1.4 M17.7 6.3l1.4-1.4",
            "moon": "M20 14.5A8 8 0 1 1 9.5 4a10.5 10.5 0 0 0 10.5 10.5z",
            "chevronLeft": "M15 6l-6 6 6 6",
            "chevronRight": "M9 6l6 6-6 6",
            "chevronUp": "M6 15l6-6 6 6",
            "chevronDown": "M6 9l6 6 6-6",
            "close": "M6 6l12 12 M18 6L6 18",
            "check": "M5 12.5l4.5 4.5L19 7.5",
            "plus": "M12 5v14 M5 12h14",
            "minus": "M5 12h14",
            "undo": "M9 14L4 9l5-5 M4 9h11a5 5 0 0 1 0 10h-3",
            "swap": "M7 4L3 8l4 4 M3 8h13 M17 12l4 4-4 4 M21 16H8",
            "edit": "M4 20h4L19 9l-4-4L4 16z M13.5 6.5l4 4",
            "trash": "M4 7h16 M10 11v6 M14 11v6 M6 7l1 13h10l1-13 M9 7V4h6v3",
            "user": "M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M4 21v-1a7 7 0 0 1 7-7h2a7 7 0 0 1 7 7v1",
            "inbox": "M3 13l3-8h12l3 8v6a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z M3 13h5l1 3h6l1-3h5",
            "ok": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z M8 12.5l2.5 2.5 5.5-5.5",
            "warn": "M12 3l9.5 17h-19z M12 10v4 M12 17.5v.01",
            "danger": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z M9 9l6 6 M15 9l-6 6",
            "info": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z M12 11v5 M12 7.5v.01",
            // Match Day (P13): a ball, a card, full screen on and off.
            "goal": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z M12 8l3.8 2.8-1.5 4.4H9.7l-1.5-4.4z M12 8V3.5 M15.8 10.8l4.3-1.4 M14.3 15.2l2.7 3.6 M9.7 15.2L7 18.8 M8.2 10.8L3.9 9.4",
            "card": "M8 3h8a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H8a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z",
            "fullscreen": "M4 9V4h5 M15 4h5v5 M20 15v5h-5 M9 20H4v-5",
            "exitFullscreen": "M9 4v5H4 M15 4v5h5 M20 15h-5v5 M4 15h5v5",
            "note": "M6 3h9l4 4v14H6z M15 3v4h4 M9 12h7 M9 16h5",
            // Live tactics (P13b): a pitch with an arrow, a change of shape.
            "tactic": "M4 3h16v18H4z M4 12h16 M8 17h.01 M12 17h.01 M16 17h.01 M9 8l3-2.5 3 2.5 M12 5.5V10"
        })

    implicitWidth: size
    implicitHeight: size

    onNameChanged: {
        if (name.length > 0 && !(name in paths))
            console.warn("Icon: unknown icon name", name);
    }

    Shape {
        anchors.centerIn: parent
        width: Theme.iconGrid
        height: Theme.iconGrid
        scale: root.size / Theme.iconGrid
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.color
            strokeWidth: Theme.iconStrokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: root.name in root.paths ? root.paths[root.name] + root.boundsGuide : ""
            }
        }
    }
}
