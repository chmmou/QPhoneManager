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

    Shortcut {
        sequences: [StandardKey.Refresh]
        enabled: root.canSync
        onActivated: root.syncRequested()
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: root.settingsRequested()
    }

    Shortcut {
        sequences: [StandardKey.Quit]
        onActivated: root.quitRequested()
    }

    Shortcut {
        sequences: [StandardKey.HelpContents]
        onActivated: root.helpRequested()
    }
}
