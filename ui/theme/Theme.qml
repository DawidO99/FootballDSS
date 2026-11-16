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
        readonly property int xs: 2
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

        // The ratio bands of the load chart: the status colors (ok, warn, danger) at low alpha, so
        // the line on top stays readable. The legend names each band with its numbers too.
        readonly property color bandNormal: dark ? "#3322C55E" : "#2615803D"
        readonly property color bandElevated: dark ? "#40F59E0B" : "#33B45309"
        readonly property color bandHigh: dark ? "#40EF4444" : "#2EB91C1C"
        // The ratio line, drawn over the bands.
        readonly property color ratioLine: dark ? "#E7ECF4" : "#111827"
        // Legend swatch of a line and a band; width of the tooltip; the day marker under the mouse.
        readonly property int legendSwatchWidth: 18
        readonly property int tooltipWidth: 240
        readonly property int hoverMarkerWidth: 1
        // Share of a day taken by its bar.
        readonly property real barWidth: 0.7
        // The least room for one label of a date axis.
        readonly property int minTickSpacing: 56
    }

    // The tactics board (P12): grass, lines and zones of the pitch, and the player tokens. Only the
    // grass follows the theme, so a token reads the same on either.
    component PitchStyle: QtObject {
        required property bool dark
        required property int tokenSize

        readonly property color grass: dark ? "#1F4D36" : "#2E7D4F"
        readonly property color grassStripe: dark ? "#245A3F" : "#338A57"
        readonly property color line: "#E8F5EC"
        // The zones while a token is dragged, and the zone under the pointer.
        readonly property color zoneBorder: "#80E8F5EC"
        readonly property color zoneActive: "#40E8F5EC"
        readonly property color token: "#F8FAFC"
        readonly property color tokenText: "#111827"
        readonly property color emptySlot: "#B3E8F5EC"
        readonly property color labelBackground: "#CC111827"
        readonly property color labelText: "#F8FAFC"
        readonly property int lineWidth: 2
        readonly property int stripes: 8
        // Width to height of the drawn pitch; a real one is 68 to 105, a wider one leaves the
        // names of a row of tokens more room.
        readonly property real aspect: 0.74
        readonly property int labelWidth: 76
        readonly property int labelHeight: 18
        readonly property int issueIconSize: 16
        // A drag shorter than this is a click.
        readonly property int dragThreshold: 8
        // The size the board asks for when nothing constrains it.
        readonly property int preferredWidth: 360
        // The markings, in metres of a 68 × 105 m pitch.
        readonly property real widthMetres: 68
        readonly property real lengthMetres: 105
        readonly property real centreCircleMetres: 18.3
        readonly property real penaltyAreaWidthMetres: 40.3
        readonly property real penaltyAreaDepthMetres: 16.5
        readonly property real goalAreaWidthMetres: 18.3
        readonly property real goalAreaDepthMetres: 5.5
    }

    // The size a window opens with and the smallest it can be made.
    component WindowSize: QtObject {
        required property int width
        required property int height
        required property int minimumWidth
        required property int minimumHeight
    }

    component FontSizeScale: QtObject {
        readonly property int caption: 12
        readonly property int body: 14
        readonly property int title: 18
        readonly property int h1: 24
        readonly property int display: 32
    }

    component IconSizeScale: QtObject {
        readonly property int xs: 14
        readonly property int s: 16
        readonly property int sm: 18
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
    readonly property WindowSize mainWindow: WindowSize {
        width: 1600
        height: 900
        minimumWidth: 1280
        minimumHeight: 720
    }
    // The database error screen, which opens instead of the main window.
    readonly property WindowSize errorWindow: WindowSize {
        width: 720
        height: 420
        minimumWidth: 560
        minimumHeight: 360
    }
    readonly property ChartStyle chart: ChartStyle {
        dark: theme.dark
    }
    readonly property PitchStyle pitch: PitchStyle {
        dark: theme.dark
        tokenSize: theme.hitTouch
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
    readonly property int topBarHeight: 56
    readonly property int topBarSearchWidth: 360
    readonly property int sidebarWidth: 220
    readonly property int sidebarCollapsedWidth: 64
    // The bar beside the current section of the sidebar.
    readonly property int sidebarMarkerWidth: 3
    readonly property int badgeHeight: 24
    // Added to a width measured from text (FontMetrics) for a column that must hold it: the
    // measurement and the laid-out text may round and hint differently.
    readonly property int measuredTextSlack: 4
    readonly property int emptyStateWidth: 480
    // Pre-match (P12): the bench column and the panel of the lineup issues.
    readonly property int benchColumnWidth: 228
    // The shirt number column of a player list.
    readonly property int shirtNumberWidth: 24
    readonly property int issuesPanelHeight: 196
    // The list of the slot picker of Pre-match: about seven rows with their group headings.
    readonly property int slotPickerHeight: 360
    // Icon.qml draws its paths on a square of this side and scales it to the size asked for.
    readonly property int iconGrid: 24
    readonly property int iconStrokeWidth: 2
    readonly property int borderWidth: 1
    readonly property int focusRingWidth: 2
    readonly property real disabledOpacity: 0.45
    readonly property real hoverOpacity: 0.9
    readonly property real pressedOpacity: 0.8

    // Timing in milliseconds.
    readonly property int toastDuration: 10000
    readonly property int searchDebounce: 200
    readonly property int tooltipDelay: 400
    readonly property int sidebarAnimation: 120
}
