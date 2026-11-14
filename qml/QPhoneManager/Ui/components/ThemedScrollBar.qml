import QtQuick
import QtQuick.Controls

// A scroll bar whose thumb stands out from the window by 3:1 at least.
ScrollBar {
    id: root

    // The style draws the thumb in a faint grey of its own.
    Binding {
        target: root.contentItem
        property: "color"
        value: root.pressed || root.hovered ? Theme.foregroundOnSurfaceVariant : Theme.outline
    }
}
