pragma Singleton

import QtQuick
import QtQuick.Templates as T

QtObject {
    id: root

    readonly property bool dark: Settings.colorScheme === Settings.ColorScheme.Dark
                                 || (Settings.colorScheme === Settings.ColorScheme.System
                                     && Application.styleHints.colorScheme === Qt.ColorScheme.Dark)

    readonly property color primary: root.dark ? "#4FD8D6" : "#006A6A"
    // Not pure white, here and on the error colour: the Material style drops an
    // explicit foreground that equals the default text colour of its current
    // theme. While the scheme turns from dark to light, a control still has the
    // dark theme, whose default is white, and its label then took the colour of
    // the window.
    readonly property color foregroundOnPrimary: root.dark ? "#003737" : "#FFFFFE"
    readonly property color surface: root.dark ? "#0F1514" : "#F7FAF9"
    readonly property color surfaceVariant: root.dark ? "#3F4947" : "#DAE5E3"
    readonly property color surfaceContainer: root.dark ? "#1B2120" : "#EBF0EE"
    readonly property color foregroundOnSurface: root.dark ? "#DEE4E2" : "#171D1C"
    readonly property color foregroundOnSurfaceVariant: root.dark ? "#BEC9C6" : "#3F4947"
    readonly property color outline: root.dark ? "#9AA4A1" : "#6F7977"
    readonly property color error: root.dark ? "#FFB4AB" : "#BA1A1A"
    readonly property color foregroundOnError: root.dark ? "#690005" : "#FFFFFE"
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

    // On while the user asked for less motion in the settings.
    readonly property bool reduceMotion: Settings.reduceMotion

    function icon(name: string): url {
        return "qrc:/icons/tabler/" + name + ".svg";
    }

    // Opens and closes `popup` without its motion while reduceMotion is on,
    // and gives it back when it is off again. Called once, when the popup is
    // complete; the motion it keeps is the one the style gave it.
    function calmPopup(popup: T.Popup): void {
        const enter = popup.enter;
        const exit = popup.exit;
        const apply = () => {
            popup.enter = root.reduceMotion ? null : enter;
            popup.exit = root.reduceMotion ? null : exit;
        };
        apply();
        root.reduceMotionChanged.connect(popup, apply);
    }

    // Lets the marker of `tabs` jump to the chosen tab while reduceMotion is
    // on. Called once, when the tab bar is complete.
    function calmTabBar(tabs: T.TabBar): void {
        const list = tabs.contentItem as ListView;
        if (!list)
            return;
        const duration = list.highlightMoveDuration;
        const apply = () => {
            list.highlightMoveDuration = root.reduceMotion ? 0 : duration;
        };
        apply();
        root.reduceMotionChanged.connect(list, apply);
    }
}
