import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    required property FileBrowserModel model
    // The entry being renamed; empty when a new folder is made.
    property string oldName: ""
    readonly property string problem: root.model.nameProblem(nameField.text, root.oldName)

    signal nameChosen(string name)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: TypeScale.body * 28
    modal: true
    Overlay.modal: Scrim {}
    Material.elevation: 0
    title: root.oldName.length > 0 ? qsTr("Rename %1").arg(root.oldName) : qsTr("New folder")

    footer: DialogButtonBox {
        Button {
            id: confirmButton

            objectName: "confirmNameButton"
            text: root.oldName.length > 0 ? qsTr("Rename") : qsTr("Create")
            enabled: root.problem.length === 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole

            FocusRing {
                shown: confirmButton.visualFocus
            }
        }

        Button {
            id: cancelButton

            objectName: "cancelNameButton"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: cancelButton.visualFocus
            }
        }
    }

    onOpened: {
        nameField.text = root.oldName.length > 0 ? root.oldName : qsTr("New folder");
        nameField.selectAll();
        nameField.forceActiveFocus(Qt.TabFocusReason);
    }
    onAccepted: root.nameChosen(nameField.text)

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.gridUnit

        Label {
            id: nameLabel

            text: qsTr("Name")
        }

        TextField {
            id: nameField

            objectName: "fileNameField"
            Layout.fillWidth: true
            Accessible.name: nameLabel.text
            Accessible.description: nameProblemLabel.text
            onAccepted: {
                if (root.problem.length === 0)
                    root.accept();
            }
        }

        Label {
            id: nameProblemLabel

            objectName: "fileNameProblem"
            Layout.fillWidth: true
            visible: root.problem.length > 0 && nameField.text.length > 0
            text: root.problem
            color: Theme.error
            font.pixelSize: TypeScale.hint
            wrapMode: Text.Wrap
        }
    }
}
