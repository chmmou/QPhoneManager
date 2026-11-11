pragma ComponentBehavior: Bound

import QtQuick

Row {
    id: root

    required property int bars

    spacing: Theme.gridUnit / 2
    Accessible.ignored: true

    Repeater {
        model: 4

        delegate: Rectangle {
            required property int index

            anchors.bottom: parent.bottom
            width: Theme.gridUnit * 2
            height: Theme.gridUnit * (2 + index * 1.5)
            radius: 1
            color: index < root.bars ? Theme.primary : Theme.surfaceVariant
        }
    }
}
