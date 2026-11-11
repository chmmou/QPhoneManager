pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    readonly property ContactsArea area: AppController.contacts
    required property string emptyText
    property bool editing: false
    property int pendingDelete: 0
    property int pendingExport: 0

    signal connectRequested

    objectName: "contactsPage"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: root.area.hasPhone
            padding: Theme.gridUnit * 2
            Material.background: Theme.surface

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit * 2

                ToolButton {
                    id: newButton

                    objectName: "newContactButton"
                    text: qsTr("New contact")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("plus")
                    enabled: root.area.canEdit
                    Accessible.name: newButton.text
                    ToolTip.visible: newButton.hovered
                    ToolTip.text: newButton.text
                    onClicked: root.startNew()

                    FocusRing {
                        shown: newButton.visualFocus
                    }
                }

                ToolButton {
                    id: importButton

                    objectName: "importContactsButton"
                    text: qsTr("Import contacts")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("upload")
                    enabled: root.area.canEdit && !root.area.busy
                    Accessible.name: importButton.text
                    ToolTip.visible: importButton.hovered
                    ToolTip.text: importButton.text
                    onClicked: importDialog.open()

                    FocusRing {
                        shown: importButton.visualFocus
                    }
                }

                ToolButton {
                    id: exportButton

                    objectName: "exportContactsButton"
                    text: qsTr("Export the contacts in the list")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("download")
                    enabled: root.area.list.count > 0 && !root.area.busy
                    Accessible.name: exportButton.text
                    ToolTip.visible: exportButton.hovered
                    ToolTip.text: exportButton.text
                    onClicked: root.startExport(0)

                    FocusRing {
                        shown: exportButton.visualFocus
                    }
                }

                ToolButton {
                    id: duplicatesButton

                    objectName: "duplicatesButton"
                    text: qsTr("Find duplicates")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("copy")
                    enabled: root.area.list.totalCount > 1 && !root.area.busy
                    Accessible.name: duplicatesButton.text
                    ToolTip.visible: duplicatesButton.hovered
                    ToolTip.text: duplicatesButton.text
                    onClicked: {
                        if (root.area.findDuplicates())
                            duplicatesDialog.open();
                    }

                    FocusRing {
                        shown: duplicatesButton.visualFocus
                    }
                }

                ToolButton {
                    id: accountsButton

                    objectName: "accountsButton"
                    text: qsTr("Account for new contacts")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("users")
                    enabled: root.area.canEdit && root.area.accountNames.length > 1
                    Accessible.name: accountsButton.text
                    ToolTip.visible: accountsButton.hovered
                    ToolTip.text: accountsButton.text
                    onClicked: accountMenu.popup(accountsButton, 0, accountsButton.height)

                    FocusRing {
                        shown: accountsButton.visualFocus
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ComboBox {
                    id: groupBox

                    objectName: "groupFilterBox"
                    Layout.preferredWidth: TypeScale.body * 10
                    model: [qsTr("All groups")].concat(root.area.groupNames)
                    Accessible.name: qsTr("Show group")
                    onActivated: index => root.area.list.groupFilter = index > 0 ? root.area.groupNames[index - 1] : ""

                    FocusRing {
                        shown: groupBox.visualFocus
                    }
                }

                ComboBox {
                    id: accountBox

                    objectName: "accountFilterBox"
                    Layout.preferredWidth: TypeScale.body * 12
                    visible: root.area.accountNames.length > 1
                    model: [qsTr("All accounts")].concat(root.area.accountNames)
                    Accessible.name: qsTr("Show account")
                    onActivated: index => root.area.list.accountFilter = index > 0 ? root.area.accountKeys[index - 1] : ""

                    FocusRing {
                        shown: accountBox.visualFocus
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.area.hasPhone
            color: Theme.outline
        }

        ListDetailView {
            id: listDetail

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.area.hasPhone
            listComponent: Component {
                ContactList {
                    area: root.area
                    emptyText: root.area.canEdit ? qsTr("No contacts yet. Sync the phone or add a contact.") : root.emptyText
                    onOpenRequested: listDetail.detailOpen = true
                    onDeleteRequested: luid => root.confirmDelete(luid)
                }
            }
            detailComponent: Component {
                Loader {
                    sourceComponent: root.editing ? formComponent : detailViewComponent
                }
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.area.hasPhone
            text: root.emptyText
            actionText: qsTr("Connect")
            actionIcon: "plug-connected"
            onActionTriggered: root.connectRequested()
        }
    }

    Component {
        id: detailViewComponent

        ContactDetail {
            area: root.area
            onEditRequested: root.startEdit()
            onDeleteRequested: luid => root.confirmDelete(luid)
            onExportRequested: luid => root.startExport(luid)
        }
    }

    Component {
        id: formComponent

        ContactForm {
            id: form

            area: root.area
            onFinished: root.editing = false
            onPhotoRequested: photoFileDialog.open()
            Component.onCompleted: form.focusFirstField()
        }
    }

    Shortcut {
        sequences: [StandardKey.New]
        enabled: root.visible && root.area.canEdit && !root.editing
        onActivated: root.startNew()
    }

    Connections {
        target: root.area.editor

        function onSaved(luid: int): void {
            root.editing = false;
        }

        function onConflictChanged(): void {
            if (root.area.editor.hasConflict)
                conflictDialog.open();
        }
    }

    Connections {
        target: root.area

        function onHasPhoneChanged(): void {
            root.editing = false;
        }

        function onNewContactStarted(): void {
            root.editing = true;
            listDetail.detailOpen = true;
        }
    }

    Dialog {
        id: deleteDialog

        objectName: "deleteContactDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: TypeScale.body * 28
        modal: true
        Overlay.modal: Scrim {}
        Material.elevation: 0
        title: qsTr("Delete contact %1?").arg(root.area.list.nameOf(root.pendingDelete))

        footer: DialogButtonBox {
            Button {
                id: confirmDeleteButton

                text: qsTr("Delete")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole

                FocusRing {
                    shown: confirmDeleteButton.visualFocus
                }
            }

            Button {
                id: cancelDeleteButton

                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

                FocusRing {
                    shown: cancelDeleteButton.visualFocus
                }
            }
        }

        // The tab reason makes the button show its focus ring right away; giving the
        // focus up on closing makes the next opening set that reason again.
        onOpened: cancelDeleteButton.forceActiveFocus(Qt.TabFocusReason)
        onClosed: cancelDeleteButton.focus = false
        onAccepted: root.area.removeContacts([root.pendingDelete])

        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            text: qsTr("The contact is deleted on the phone and on this computer.")
            wrapMode: Text.Wrap
        }
    }

    DuplicatesDialog {
        id: duplicatesDialog

        objectName: "duplicatesDialog"
        area: root.area
    }

    ConflictDialog {
        id: conflictDialog

        objectName: "conflictDialog"
        editor: root.area.editor
    }

    PhotoCropDialog {
        id: cropDialog

        objectName: "cropDialog"
        editor: root.area.editor
    }

    Menu {
        id: accountMenu

        Repeater {
            model: root.area.accountNames

            delegate: MenuItem {
                required property int index
                required property string modelData

                text: modelData
                checkable: true
                checked: index === root.area.defaultAccountIndex
                onTriggered: root.area.setDefaultAccount(index)
            }
        }
    }

    FileDialog {
        id: importDialog

        objectName: "importDialog"
        title: qsTr("Import contacts")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Contacts (*.vcf *.csv)"), qsTr("All files (*)")]
        onAccepted: root.area.importContacts(importDialog.selectedFile)
    }

    FileDialog {
        id: exportDialog

        objectName: "exportDialog"
        title: root.pendingExport > 0 ? qsTr("Export contact") : qsTr("Export contacts")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("vCard (*.vcf)"), qsTr("Table for spreadsheets (*.csv)")]
        defaultSuffix: exportDialog.selectedNameFilter.index === 1 ? "csv" : "vcf"
        onAccepted: root.area.exportContacts(exportDialog.selectedFile, exportDialog.selectedNameFilter.index === 1, root.pendingExport > 0 ? [root.pendingExport] : [])
    }

    FileDialog {
        id: photoFileDialog

        objectName: "photoFileDialog"
        title: qsTr("Choose a photo")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Pictures (*.jpg *.jpeg *.png *.webp *.bmp *.gif)"), qsTr("All files (*)")]
        onAccepted: {
            if (root.area.editor.loadPhotoCandidate(photoFileDialog.selectedFile))
                cropDialog.open();
        }
    }

    function startNew(): void {
        root.area.editor.newContact();
        root.editing = true;
        listDetail.detailOpen = true;
    }

    function startEdit(): void {
        if (root.area.editor.load(root.area.currentLuid)) {
            root.editing = true;
            listDetail.detailOpen = true;
        }
    }

    function confirmDelete(luid: int): void {
        if (luid <= 0 || !root.area.canEdit)
            return;
        root.pendingDelete = luid;
        deleteDialog.open();
    }

    function startExport(luid: int): void {
        root.pendingExport = luid;
        exportDialog.open();
    }
}
