import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A combo box whose outline and, while editable, selected text stay readable
// in both colour schemes.
ComboBox {
    id: root

    // The style draws the outline in its hint colour, below 3:1 against the window.
    Binding {
        target: root.background
        property: "outlineColor"
        value: !root.enabled ? root.Material.hintTextColor : root.hovered ? root.Material.primaryTextColor : Theme.outline
    }

    Binding {
        target: root.contentItem
        property: "selectedTextColor"
        value: Theme.foregroundOnPrimary
    }
}
