import QtQuick
import QtQuick.Shapes

// Line icon drawn on a 24x24 grid; color follows the theme like text does.
Item {
    id: root

    property string name
    property color color: Theme.text
    property real size: 20

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
            "plus": "M12 5v14 M5 12h14",
            "minus": "M5 12h14",
            "undo": "M9 14L4 9l5-5 M4 9h11a5 5 0 0 1 0 10h-3",
            "edit": "M4 20h4L19 9l-4-4L4 16z M13.5 6.5l4 4",
            "trash": "M4 7h16 M10 11v6 M14 11v6 M6 7l1 13h10l1-13 M9 7V4h6v3",
            "user": "M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M4 21v-1a7 7 0 0 1 7-7h2a7 7 0 0 1 7 7v1",
            "inbox": "M3 13l3-8h12l3 8v6a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z M3 13h5l1 3h6l1-3h5",
            "ok": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z M8 12.5l2.5 2.5 5.5-5.5",
            "warn": "M12 3l9.5 17h-19z M12 10v4 M12 17.5v.01",
            "danger": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z M9 9l6 6 M15 9l-6 6",
            "info": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z M12 11v5 M12 7.5v.01"
        })

    implicitWidth: size
    implicitHeight: size

    onNameChanged: {
        if (name.length > 0 && !(name in paths))
            console.warn("Icon: unknown icon name", name);
    }

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: root.size / 24
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.color
            strokeWidth: 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: root.paths[root.name] ?? ""
            }
        }
    }
}
