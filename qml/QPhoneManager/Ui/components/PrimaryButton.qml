import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The main action of a view: filled with the primary colour unless it is flat,
// its label in the theme colour made for that fill.
Button {
    id: root

    // Not "highlighted": the style draws a highlighted label in a colour of its
    // own, which is white on the light primary colour of the dark scheme.
    Material.background: root.flat ? undefined : Theme.primary
    Material.foreground: root.flat ? undefined : Theme.foregroundOnPrimary
}
