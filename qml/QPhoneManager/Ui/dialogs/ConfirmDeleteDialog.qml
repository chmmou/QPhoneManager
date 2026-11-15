import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Dialog {
    id: root

    property string message: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: TypeScale.body * 28
    modal: true
    Overlay.modal: Scrim {}
    Material.elevation: 0

    footer: DialogButtonBox {
        DialogButton {
            id: confirmButton

            text: qsTr("Delete")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }

        DialogButton {
            id: cancelButton

            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    onOpened: cancelButton.forceActiveFocus(Qt.PopupFocusReason)
    Component.onCompleted: Theme.calmPopup(root)

    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        text: root.message
        wrapMode: Text.Wrap
    }
}
