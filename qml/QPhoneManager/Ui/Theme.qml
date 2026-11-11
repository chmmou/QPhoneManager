pragma Singleton

import QtQuick

QtObject {
    id: root

    readonly property bool dark: Settings.colorScheme === Settings.ColorScheme.Dark
                                 || (Settings.colorScheme === Settings.ColorScheme.System
                                     && Application.styleHints.colorScheme === Qt.ColorScheme.Dark)

    readonly property color primary: root.dark ? "#4FD8D6" : "#006A6A"
    readonly property color foregroundOnPrimary: root.dark ? "#003737" : "#FFFFFF"
    readonly property color surface: root.dark ? "#0F1514" : "#F7FAF9"
    readonly property color surfaceVariant: root.dark ? "#3F4947" : "#DAE5E3"
    readonly property color surfaceContainer: root.dark ? "#1B2120" : "#EBF0EE"
    readonly property color foregroundOnSurface: root.dark ? "#DEE4E2" : "#171D1C"
    readonly property color foregroundOnSurfaceVariant: root.dark ? "#BEC9C6" : "#3F4947"
    readonly property color outline: root.dark ? "#9AA4A1" : "#6F7977"
    readonly property color error: root.dark ? "#FFB4AB" : "#BA1A1A"
    readonly property color foregroundOnError: root.dark ? "#690005" : "#FFFFFF"
    readonly property color success: root.dark ? "#8DD79D" : "#1E6B32"
    readonly property color warning: root.dark ? "#F2BF48" : "#7A5900"
    // The same in both schemes: half-transparent black steps the window back.
    readonly property color scrim: "#80000000"

    readonly property int gridUnit: 4
    readonly property int cardRadius: 12
    readonly property int sidebarWidth: 220
    readonly property int compactSidebarWidth: 64
    readonly property int compactWindowWidth: 1200
    readonly property int narrowContentWidth: 900
    readonly property int sidebarIconSize: 24
    readonly property int listIconSize: 20

    function icon(name: string): url {
        return "qrc:/icons/tabler/" + name + ".svg";
    }
}
