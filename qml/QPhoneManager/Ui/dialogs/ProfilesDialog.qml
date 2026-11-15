pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    property string pendingId: ""
    property string pendingName: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 36, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 36)
    height: Math.min(TypeScale.body * 30, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 30)
    modal: true
    Overlay.modal: Scrim {}
    // The raised look of Material draws its dialog through a shader effect,
    // which the software renderer leaves out together with the background.
    Material.elevation: 0
    title: qsTr("Phones on this computer")

    footer: DialogButtonBox {
        DialogButton {
            id: closeButton

            objectName: "profilesCloseButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    onOpened: closeButton.forceActiveFocus(Qt.PopupFocusReason)
    Component.onCompleted: Theme.calmPopup(root)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            visible: AppController.devices.count === 0
            text: qsTr("No phones yet. Connect a phone to add it here.")
            wrapMode: Text.Wrap
        }

        ListView {
            id: list

            objectName: "profileList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The list scrolls when there are more phones than fit.
            clip: true
            model: AppController.devices
            spacing: Theme.gridUnit * 2
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Phones on this computer")
            ScrollBar.vertical: ThemedScrollBar {}

            delegate: RowLayout {
                id: profileRow

                required property string deviceId
                required property string displayName
                required property string phoneModel
                required property string lastConnection
                required property bool current

                width: ListView.view.width
                spacing: Theme.gridUnit * 2

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        objectName: "profileName"
                        text: profileRow.current ? qsTr("%1 (shown now)").arg(profileRow.displayName) : profileRow.displayName
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: profileRow.lastConnection.length > 0 ? qsTr("%1, last connected %2").arg(profileRow.phoneModel).arg(profileRow.lastConnection) : profileRow.phoneModel
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        wrapMode: Text.Wrap
                    }
                }

                Button {
                    id: showButton

                    text: qsTr("Show")
                    flat: true
                    enabled: !profileRow.current
                    Accessible.name: qsTr("Show %1").arg(profileRow.displayName)
                    onClicked: AppController.selectDevice(profileRow.deviceId)

                    FocusRing {
                        shown: showButton.activeFocus
                    }
                }

                Button {
                    id: renameButton

                    text: qsTr("Rename")
                    flat: true
                    Accessible.name: qsTr("Rename %1").arg(profileRow.displayName)
                    onClicked: {
                        root.pendingId = profileRow.deviceId;
                        root.pendingName = profileRow.displayName;
                        renameDialog.open();
                    }

                    FocusRing {
                        shown: renameButton.activeFocus
                    }
                }

                Button {
                    id: removeButton

                    text: qsTr("Remove")
                    flat: true
                    Accessible.name: qsTr("Remove %1").arg(profileRow.displayName)
                    onClicked: {
                        root.pendingId = profileRow.deviceId;
                        root.pendingName = profileRow.displayName;
                        removeDialog.open();
                    }

                    FocusRing {
                        shown: removeButton.activeFocus
                    }
                }
            }
        }
    }

    Dialog {
        id: renameDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: TypeScale.body * 26
        modal: true
        Overlay.modal: Scrim {}
        Material.elevation: 0
        title: qsTr("Rename %1").arg(root.pendingName)

        footer: DialogButtonBox {
            DialogButton {
                text: qsTr("Rename")
                enabled: nameField.text.trim().length > 0
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }

            DialogButton {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onOpened: {
            nameField.text = root.pendingName;
            nameField.selectAll();
            nameField.forceActiveFocus(Qt.PopupFocusReason);
        }
        onAccepted: AppController.renameDevice(root.pendingId, nameField.text)
        Component.onCompleted: Theme.calmPopup(renameDialog)

        ThemedTextField {
            id: nameField

            anchors.left: parent.left
            anchors.right: parent.right
            maximumLength: 64
            Accessible.name: qsTr("New name")
            onAccepted: {
                if (nameField.text.trim().length > 0)
                    renameDialog.accept();
            }
        }
    }

    Dialog {
        id: removeDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: TypeScale.body * 28
        modal: true
        Overlay.modal: Scrim {}
        Material.elevation: 0
        title: qsTr("Remove %1 from this computer?").arg(root.pendingName)

        footer: DialogButtonBox {
            DialogButton {
                text: qsTr("Remove")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }

            DialogButton {
                id: cancelRemoveButton

                objectName: "cancelRemoveProfileButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onOpened: cancelRemoveButton.forceActiveFocus(Qt.PopupFocusReason)
        onAccepted: AppController.removeDevice(root.pendingId)
        Component.onCompleted: Theme.calmPopup(removeDialog)

        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            text: qsTr("The stored data and the remembered PIN of this phone are deleted from this computer. Nothing changes on the phone.")
            wrapMode: Text.Wrap
        }
    }
}
