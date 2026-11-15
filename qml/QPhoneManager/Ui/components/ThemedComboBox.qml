import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A combo box with the subtle resting outline of the program, an edge around
// its list, and selected text that stays readable in both colour schemes.
ComboBox {
    id: root

    // The style sets its list apart by a shadow alone, which the software
    // renderer does not draw, so the list got lost on a dialog of its colour.
    popup.background: Rectangle {
        radius: Theme.gridUnit
        color: Theme.surfaceContainer
        border.width: 1
        border.color: Theme.outlineVariant
    }

    // At rest and switched off the outline takes the colour of the
    // separators; hover and focus keep the colours of the style.
    Binding {
        target: root.background
        property: "outlineColor"
        value: root.enabled && root.hovered ? root.Material.primaryTextColor : Theme.outlineVariant
    }

    Binding {
        target: root.contentItem
        property: "selectedTextColor"
        value: Theme.foregroundOnPrimary
    }
}
