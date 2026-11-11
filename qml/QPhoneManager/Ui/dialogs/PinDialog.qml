import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    property int remainingTries: 10
    property bool wrongPin: false
    property bool canRemember: true

    signal pinEntered(string pin, bool remember)
    signal cancelled

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 30, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 30)
    modal: true
    Overlay.modal: Scrim {}
    // The raised look of Material draws its dialog through a shader effect,
    // which the software renderer leaves out together with the background.
    Material.elevation: 0
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Enter the PIN")

    footer: DialogButtonBox {
        Button {
            id: connectButton

            objectName: "pinConnectButton"
            text: qsTr("Connect")
            enabled: pinField.text.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole

            FocusRing {
                shown: connectButton.visualFocus
            }
        }

        Button {
            id: cancelButton

            objectName: "pinCancelButton"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: cancelButton.visualFocus
            }
        }
    }

    onOpened: {
        pinField.clear();
        pinField.forceActiveFocus(Qt.PopupFocusReason);
    }
    onClosed: pinField.clear()
    onAccepted: root.pinEntered(pinField.text, rememberBox.checked)
    onRejected: root.cancelled()

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: qsTr("MyPhoneExplorer Client on the phone is protected with a PIN. Enter the PIN you set in the app.")
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.wrongPin
            spacing: Theme.gridUnit * 2

            TintedIcon {
                Layout.preferredWidth: Theme.listIconSize
                Layout.preferredHeight: Theme.listIconSize
                name: "alert-triangle"
                color: Theme.error
            }

            Label {
                objectName: "wrongPinLabel"
                Layout.fillWidth: true
                text: root.remainingTries <= 0 ? qsTr("The PIN is wrong. After one more wrong PIN, the app on the phone stops.") : qsTr("The PIN is wrong. %n tries left.", "", root.remainingTries)
                color: Theme.error
                wrapMode: Text.Wrap
                Accessible.role: Accessible.AlertMessage
            }
        }

        TextField {
            id: pinField

            objectName: "pinField"
            Layout.fillWidth: true
            placeholderText: qsTr("PIN")
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            Accessible.name: qsTr("PIN")
            onAccepted: {
                if (pinField.text.length > 0)
                    root.accept();
            }
        }

        CheckBox {
            id: rememberBox

            objectName: "rememberPinBox"
            Layout.fillWidth: true
            visible: root.canRemember
            text: qsTr("Remember the PIN in the keyring of this computer")
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("The connection is not encrypted. Use it only in a network you trust.")
            font.pixelSize: TypeScale.hint
            color: Theme.foregroundOnSurfaceVariant
            wrapMode: Text.Wrap
        }
    }
}
