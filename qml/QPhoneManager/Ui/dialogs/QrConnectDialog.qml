import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    readonly property QrCodeProvider provider: AppController.qrCode

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 30, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 30)
    modal: true
    Overlay.modal: Scrim {}
    // The raised look of Material draws its dialog through a shader effect,
    // which the software renderer leaves out together with the background.
    Material.elevation: 0
    title: qsTr("Connect with a QR code")

    footer: DialogButtonBox {
        DialogButton {
            id: closeButton

            objectName: "qrCloseButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    // The tab reason makes the button show its focus ring right away; giving the
    // focus up on closing makes the next opening set that reason again.
    onOpened: {
        root.provider.active = true;
        closeButton.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        root.provider.active = false;
        closeButton.focus = false;
    }
    Component.onCompleted: Theme.calmPopup(root)

    Connections {
        target: AppController

        function onConnectionStateChanged(): void {
            if (AppController.connected && root.opened)
                root.close();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: qsTr("Open MyPhoneExplorer Client on the phone, choose the QR code scanner and point the camera at this code.")
            wrapMode: Text.Wrap
        }

        QrCodeImage {
            id: code

            objectName: "qrCode"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: TypeScale.body * 16
            Layout.preferredHeight: TypeScale.body * 16
            visible: code.valid
            text: root.provider.text
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("QR code for MyPhoneExplorer Client")
        }

        Label {
            objectName: "qrNoAddress"
            Layout.fillWidth: true
            visible: root.provider.active && !code.valid
            text: qsTr("This computer has no address in a local network. Connect it to the same Wi-Fi as the phone, then open this window again.")
            color: Theme.error
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            visible: code.valid
            text: qsTr("The phone connects to this computer at %1. Both need to be in the same network.").arg(root.provider.ownAddress)
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            visible: AppController.listening || AppController.connecting
            spacing: Theme.gridUnit * 2

            BusyIndicator {
                Layout.preferredWidth: TypeScale.body * 2
                Layout.preferredHeight: TypeScale.body * 2
                running: root.opened
                Accessible.name: qsTr("Waiting")
            }

            Label {
                Layout.fillWidth: true
                text: AppController.connecting ? qsTr("Connecting to the phone.") : qsTr("Waiting for the phone.")
                wrapMode: Text.Wrap
            }
        }
    }
}
