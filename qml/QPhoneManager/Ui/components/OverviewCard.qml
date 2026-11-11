import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: root

    required property string title
    required property string iconName
    default property alias content: body.data

    padding: Theme.gridUnit * 4
    Material.foreground: Theme.foregroundOnSurface
    Accessible.role: Accessible.Grouping
    Accessible.name: root.title
    background: Rectangle {
        radius: Theme.cardRadius
        color: Theme.surfaceContainer
        border.width: 1
        border.color: Theme.surfaceVariant
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gridUnit * 2

            TintedIcon {
                Layout.preferredWidth: Theme.sidebarIconSize
                Layout.preferredHeight: Theme.sidebarIconSize
                name: root.iconName
                color: Theme.primary
            }

            Label {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: TypeScale.section
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            id: body

            Layout.fillWidth: true
            spacing: Theme.gridUnit * 2
        }
    }
}
