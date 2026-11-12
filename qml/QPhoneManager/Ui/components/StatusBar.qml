import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ToolBar {
    id: root

    readonly property int battery: AppController.status.batteryPercent

    Material.foreground: Theme.foregroundOnSurfaceVariant
    background: Rectangle {
        color: Theme.surfaceVariant
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.gridUnit * 3
        anchors.rightMargin: Theme.gridUnit * 3
        spacing: Theme.gridUnit * 6

        Label {
            objectName: "connectionStatus"
            Layout.fillWidth: true
            text: root.connectionText()
            font.pixelSize: TypeScale.hint
            elide: Text.ElideRight
        }

        Label {
            objectName: "batteryStatus"
            visible: AppController.connected && root.battery >= 0
            text: AppController.status.charging ? qsTr("Battery %1 %, charging").arg(root.battery) : qsTr("Battery %1 %").arg(root.battery)
            font.pixelSize: TypeScale.hint
        }

        Label {
            objectName: "transferStatus"
            visible: AppController.transfers.active
            text: AppController.transfers.statusText
            font.pixelSize: TypeScale.hint
        }

        Label {
            objectName: "listeningStatus"
            visible: AppController.listening
            text: qsTr("Waiting for the phone to connect")
            font.pixelSize: TypeScale.hint
        }
    }

    function connectionText(): string {
        switch (AppController.connectionState) {
        case AppController.ConnectionState.Connected:
            return qsTr("Connected over %1").arg(AppController.transportLabel);
        case AppController.ConnectionState.Connecting:
            return qsTr("Connecting");
        case AppController.ConnectionState.WaitingForPin:
            return qsTr("Waiting for the PIN");
        case AppController.ConnectionState.Lost:
            return qsTr("Connection lost");
        default:
            return qsTr("Not connected");
        }
    }
}
