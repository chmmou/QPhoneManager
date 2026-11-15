import QtQuick

// The outline of the current row, tile or day of a view, shown while the view
// has the focus and the user works from the keyboard. The fill of the row
// marks the selection on its own otherwise. Inset, so that the focus ring of
// the view never covers it.
Rectangle {
    id: root

    required property bool current
    required property bool viewFocused
    property real inset: Theme.gridUnit

    anchors.fill: parent
    anchors.margins: root.inset
    visible: FocusVisibility.keyboard && root.current && root.viewFocused
    radius: Theme.gridUnit
    color: "transparent"
    border.width: 1
    border.color: Theme.primary
}
