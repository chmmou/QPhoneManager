import QtQuick
import QtQuick.Controls

// A narrow scroll bar with a thumb in the accent colour and no track. The
// thumb stands out from the window by more than 3:1 in both schemes.
ScrollBar {
    id: root

    padding: Theme.gridUnit / 2

    // The style keeps its fade in and out; it animates whatever thumb and
    // track the bar has.
    contentItem: Rectangle {
        implicitWidth: Theme.gridUnit * 1.5
        implicitHeight: Theme.gridUnit * 1.5
        radius: Math.min(width, height) / 2
        color: Theme.primary
        opacity: 0
    }

    background: Item {}
}
