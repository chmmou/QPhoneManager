import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A text area whose placeholder, outline and selected text stay readable in
// both colour schemes. The placeholder may serve as the label of the area.
TextArea {
    id: root

    selectedTextColor: Theme.foregroundOnPrimary
    placeholderTextColor: !root.enabled ? root.Material.hintTextColor : root.activeFocus ? Theme.primary : Theme.foregroundOnSurfaceVariant

    // The style draws the outline in its hint colour, below 3:1 against the window.
    Binding {
        target: root.background
        property: "outlineColor"
        value: !root.enabled ? root.Material.hintTextColor : root.hovered ? root.Material.primaryTextColor : Theme.outline
    }
}
