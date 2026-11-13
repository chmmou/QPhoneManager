pragma ComponentBehavior: Bound

import QtQuick
import QtQml.Models

Item {
    id: root

    required property int sectionCount
    property bool canSearch: false
    property bool canSync: false

    signal sectionRequested(int index)
    signal searchRequested
    signal syncRequested
    signal quitRequested
    signal settingsRequested
    signal helpRequested

    Instantiator {
        model: Math.min(root.sectionCount, 9)

        delegate: Shortcut {
            required property int index

            sequence: "Ctrl+" + (index + 1)
            onActivated: root.sectionRequested(index)
        }
    }

    Shortcut {
        sequences: [StandardKey.Find]
        enabled: root.canSearch
        onActivated: root.searchRequested()
    }

    // StandardKey.Refresh and StandardKey.Quit depend on the desktop theme
    // and resolve to nothing on some. The program runs on Linux only, where
    // F5 and Ctrl+Q are the usual keys; listing them next to the standard
    // keys would register the same key twice on the themes that know it.
    Shortcut {
        sequence: "F5"
        enabled: root.canSync
        onActivated: root.syncRequested()
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: root.settingsRequested()
    }

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: root.quitRequested()
    }

    Shortcut {
        sequences: [StandardKey.HelpContents]
        onActivated: root.helpRequested()
    }
}
