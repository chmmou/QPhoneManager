import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A menu with an outline, so that it stands apart from a page of its own colour.
Menu {
    id: root

    // The style sets the menu apart by a shadow alone, which the software
    // renderer does not draw.
    background: Rectangle {
        implicitWidth: 200
        implicitHeight: root.Material.menuItemHeight
        radius: root.Material.roundedScale
        color: root.Material.dialogColor
        border.width: 1
        border.color: Theme.outline
    }
}
