import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    required property TransferQueue transfers

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: TypeScale.body * 30
    modal: true
    Overlay.modal: Scrim {}
    Material.elevation: 0
    title: qsTr("%1 exists already").arg(root.transfers.conflictName)
    // The dialog closes only with an answer; Escape answers "Skip".
    closePolicy: Popup.NoAutoClose

    footer: DialogButtonBox {
        Button {
            id: replaceButton

            objectName: "replaceFileButton"
            text: qsTr("Replace")
            onClicked: root.answer(TransferQueue.Replace)

            FocusRing {
                shown: replaceButton.visualFocus
            }
        }

        Button {
            id: keepBothButton

            objectName: "keepBothFilesButton"
            text: qsTr("Keep both")
            onClicked: root.answer(TransferQueue.KeepBoth)

            FocusRing {
                shown: keepBothButton.visualFocus
            }
        }

        Button {
            id: skipButton

            objectName: "skipFileButton"
            text: qsTr("Skip")
            onClicked: root.answer(TransferQueue.Skip)

            FocusRing {
                shown: skipButton.visualFocus
            }
        }
    }

    // Keeping both loses nothing, so it takes the focus first.
    onOpened: keepBothButton.forceActiveFocus(Qt.TabFocusReason)
    onClosed: keepBothButton.focus = false

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.gridUnit * 3

        Label {
            objectName: "fileConflictText"
            Layout.fillWidth: true
            text: root.transfers.conflictOnPhone ? qsTr("The folder on the phone already has a file with this name. Replace it, keep both files, or skip this one.") : qsTr("The folder on this computer already has a file with this name. Replace it, keep both files, or skip this one.")
            wrapMode: Text.Wrap
        }

        CheckBox {
            id: forAllBox

            objectName: "conflictForAllBox"
            Layout.fillWidth: true
            text: qsTr("Do the same for the other files of this transfer")

            FocusRing {
                shown: forAllBox.visualFocus
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: root.opened
        onActivated: root.answer(TransferQueue.Skip)
    }

    // The next file with a taken name can come right away; the dialog then
    // stays open and shows it.
    function answer(choice: int): void {
        const forAll = forAllBox.checked;
        forAllBox.checked = false;
        root.transfers.resolveConflict(root.transfers.conflictId, choice, forAll);
        if (root.transfers.conflictId === 0)
            root.close();
        else
            keepBothButton.forceActiveFocus(Qt.TabFocusReason);
    }
}
