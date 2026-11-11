import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: root

    property string dataDate: ""

    signal connectRequested

    Material.background: Theme.surfaceVariant
    Material.foreground: Theme.foregroundOnSurface
    padding: Theme.gridUnit * 3

    RowLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: root.dataDate.length > 0
                  ? qsTr("Not connected. Showing the data from %1.").arg(root.dataDate)
                  : qsTr("Not connected.")
            wrapMode: Text.Wrap
        }

        Button {
            id: connectButton

            objectName: "bannerConnectButton"
            text: qsTr("Connect")
            icon.source: Theme.icon("plug-connected")
            onClicked: root.connectRequested()

            FocusRing {
                shown: connectButton.visualFocus
            }
        }
    }
}
