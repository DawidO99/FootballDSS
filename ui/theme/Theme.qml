pragma Singleton

import QtQuick

// Single source of colors, fonts, spacing and sizes. Views never use color literals.
QtObject {
    id: theme

    component SpacingScale: QtObject {
        readonly property int xs: 4
        readonly property int s: 8
        readonly property int m: 12
        readonly property int l: 16
        readonly property int xl: 24
        readonly property int xxl: 32
    }

    component RadiusScale: QtObject {
        readonly property int s: 4
        readonly property int m: 8
        readonly property int l: 12
    }

    // Charts. Series colors are assigned in this fixed order and never cycled; they are checked
    // with the protanopia/deuteranopia simulation and kept apart from the status colors (ADR-006).
    component ChartStyle: QtObject {
        required property bool dark

        readonly property color series1: dark ? "#0280F7" : "#1387FE" // blue
        readonly property color series2: dark ? "#C0149A" : "#880278" // magenta
        readonly property color series3: dark ? "#AC7D1B" : "#AE931D" // ochre
        readonly property color series4: dark ? "#C659FC" : "#6E337B" // purple
        readonly property color series5: dark ? "#049886" : "#10A49E" // teal
        readonly property list<color> series: [series1, series2, series3, series4, series5]

        readonly property int lineWidth: 2
        readonly property int markerSize: 8
    }

    component FontSizeScale: QtObject {
        readonly property int caption: 12
        readonly property int body: 14
        readonly property int title: 18
        readonly property int h1: 24
        readonly property int display: 32
    }

    component IconSizeScale: QtObject {
        readonly property int s: 16
        readonly property int m: 20
        readonly property int l: 48
    }

    // Persisted in AppSettings; toggles write AppSettings.darkTheme, never this property.
    readonly property bool dark: AppSettings.darkTheme

    // Dark: deep navy / graphite with a single violet accent. Light: cool greys, same accent hue.
    readonly property color bg: dark ? "#0D1321" : "#F2F4F8"
    readonly property color surface: dark ? "#151D2E" : "#FFFFFF"
    readonly property color surfaceAlt: dark ? "#1E2940" : "#E7EBF2"
    readonly property color border: dark ? "#2B3752" : "#CBD3E1"
    readonly property color text: dark ? "#E7ECF4" : "#111827"
    readonly property color textMuted: dark ? "#8E9AB2" : "#5B6578"
    readonly property color accent: dark ? "#7C3AED" : "#6D28D9"
    readonly property color accentText: "#FFFFFF"
    readonly property color ok: dark ? "#22C55E" : "#15803D"
    readonly property color warn: dark ? "#F59E0B" : "#B45309"
    readonly property color danger: dark ? "#EF4444" : "#B91C1C"
    readonly property color info: dark ? "#38BDF8" : "#0369A1"

    // Tables, selection and modal overlays (UI kit, P06b).
    readonly property color rowAlt: dark ? "#19223A" : "#F7F9FC"
    readonly property color rowHover: dark ? "#243049" : "#EDF0F6"
    readonly property color selection: dark ? "#33296B" : "#E6DEFB"
    readonly property color scrim: dark ? "#B3060A12" : "#80111827"

    readonly property SpacingScale spacing: SpacingScale {}
    readonly property RadiusScale radius: RadiusScale {}
    readonly property FontSizeScale fontSize: FontSizeScale {}
    readonly property IconSizeScale iconSize: IconSizeScale {}
    readonly property ChartStyle chart: ChartStyle {
        dark: theme.dark
    }

    // Minimum hit targets: mouse + keyboard screens / touch screens (Match Day, tactics board).
    readonly property int hitMin: 32
    readonly property int hitTouch: 44

    // Controls (UI kit, P06b).
    readonly property int controlHeight: hitMin + spacing.xs
    readonly property int controlHeightTouch: hitTouch
    readonly property int rowHeight: 36
    readonly property int tableHeaderHeight: 40
    readonly property int fieldWidth: 280
    readonly property int popupMaxHeight: 320
    readonly property int dialogWidthS: 400
    readonly property int dialogWidthM: 560
    readonly property int dialogWidthL: 800
    readonly property int toastWidth: 480
    readonly property int setupCardWidth: 640
    readonly property int profileTileWidth: 168
    readonly property int profileTileHeight: 152
    readonly property int avatarSize: 56
    readonly property int pinKeyWidth: 72
    readonly property int borderWidth: 1
    readonly property int focusRingWidth: 2
    readonly property real disabledOpacity: 0.45
    readonly property real hoverOpacity: 0.9
    readonly property real pressedOpacity: 0.8

    // Timing in milliseconds.
    readonly property int toastDuration: 10000
    readonly property int searchDebounce: 200
    readonly property int tooltipDelay: 400
}
