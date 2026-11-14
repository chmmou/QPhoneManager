import QtQuick

// The outline of the current row, tile or day of a view: a thin one in the
// outline colour, a strong one in the primary colour while the view has the
// focus. Inset, so that the focus ring of the view never covers it.
Rectangle {
    id: root

    required property bool current
    required property bool viewFocused
    property real inset: Theme.gridUnit

    anchors.fill: parent
    anchors.margins: root.inset
    visible: root.current
    radius: Theme.gridUnit
    color: "transparent"
    border.width: root.viewFocused ? 2 : 1
    border.color: root.viewFocused ? Theme.primary : Theme.outline
}
