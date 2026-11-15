import QtQuick
import QtQuick.Controls

// A button in the footer of a dialog, flat on the dialog colour, with the
// focus ring of the program.
Button {
    id: root

    // On the grey fill of a raised button, the primary colour of the light
    // scheme stays below the contrast that text needs.
    flat: true

    FocusRing {
        shown: root.activeFocus
    }
}
