import QtQuick
import QtQuick.Controls

ItemDelegate {
    id: root

    required property string iconName
    property bool compact: false

    display: root.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
    icon.source: Theme.icon(root.iconName)
    icon.width: Theme.sidebarIconSize
    icon.height: Theme.sidebarIconSize
    // Without visible text the control would lose its name.
    Accessible.name: root.text
    ToolTip.visible: root.compact && root.hovered
    ToolTip.text: root.text

    // The highlight fill of the style is too faint to mark the current section on its own.
    Rectangle {
        objectName: "currentSectionMarker"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.gridUnit
        height: parent.height - Theme.gridUnit * 2
        visible: root.highlighted
        radius: Theme.gridUnit / 2
        color: Theme.primary
    }

    FocusRing {
        shown: root.activeFocus
    }
}
