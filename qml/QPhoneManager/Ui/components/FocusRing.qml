import QtQuick

Rectangle {
    id: root

    required property bool shown

    anchors.fill: parent
    visible: root.shown
    color: "transparent"
    radius: Theme.gridUnit
    border.width: 2
    border.color: Theme.primary
}
