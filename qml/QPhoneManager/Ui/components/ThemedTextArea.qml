import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A text area with the subtle resting outline of the program, and a
// placeholder and selected text that stay readable in both colour schemes.
// The placeholder may serve as the label of the area.
TextArea {
    id: root

    selectedTextColor: Theme.foregroundOnPrimary
    placeholderTextColor: !root.enabled ? root.Material.hintTextColor : root.activeFocus ? Theme.primary : Theme.foregroundOnSurfaceVariant

    // At rest and switched off the outline takes the colour of the
    // separators; hover and focus keep the colours of the style.
    Binding {
        target: root.background
        property: "outlineColor"
        value: root.enabled && root.hovered ? root.Material.primaryTextColor : Theme.outlineVariant
    }
}
