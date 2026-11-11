import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ToolBar {
    id: root

    required property bool connected

    Material.foreground: Theme.foregroundOnSurfaceVariant
    background: Rectangle {
        color: Theme.surfaceVariant
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.gridUnit * 3
        anchors.rightMargin: Theme.gridUnit * 3

        Label {
            objectName: "connectionStatus"
            Layout.fillWidth: true
            text: root.connected ? qsTr("Connected") : qsTr("Not connected")
            font.pixelSize: TypeScale.hint
            elide: Text.ElideRight
        }
    }
}
