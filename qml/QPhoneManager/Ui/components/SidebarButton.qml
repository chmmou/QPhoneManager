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

    FocusRing {
        shown: root.visualFocus
    }
}
