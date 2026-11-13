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
        Button {
            id: confirmButton

            text: qsTr("Delete")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole

            FocusRing {
                shown: confirmButton.visualFocus
            }
        }

        Button {
            id: cancelButton

            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: cancelButton.visualFocus
            }
        }
    }

    // The tab reason makes the button show its focus ring right away; giving the
    // focus up on closing makes the next opening set that reason again.
    onOpened: cancelButton.forceActiveFocus(Qt.TabFocusReason)
    onClosed: cancelButton.focus = false
    Component.onCompleted: Theme.calmPopup(root)

    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        text: root.message
        wrapMode: Text.Wrap
    }
}
