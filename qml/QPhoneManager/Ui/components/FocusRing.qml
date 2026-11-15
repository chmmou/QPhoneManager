import QtQuick

// The focus ring of the program over its parent. `shown` says that the item
// it marks holds the active focus; the ring is drawn only while the user
// works from the keyboard, see FocusVisibility.
Rectangle {
    id: root

    required property bool shown

    anchors.fill: parent
    // Read first, so that the watcher exists before the focus first moves.
    visible: FocusVisibility.keyboard && root.shown
    color: "transparent"
    radius: Theme.gridUnit
    border.width: 2
    border.color: Theme.primary
}
