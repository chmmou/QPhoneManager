pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    readonly property MessagesArea area: AppController.messages
    required property string emptyText
    readonly property bool showingScheduled: viewTabs.currentIndex === 1
    property list<int> pendingDelete: []
    property bool pendingDeleteConversation: false
    property bool pendingDeleteScheduled: false
    property int pendingAttachmentLuid: 0
    property int pendingAttachmentPosition: -1
    property bool pendingExportAll: false

    signal connectRequested

    objectName: "messagesPage"

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

                TabBar {
                    id: viewTabs

                    objectName: "messageViewTabs"
                    Accessible.name: qsTr("Show messages")
                    onCurrentIndexChanged: root.area.composing = false

                    TabButton {
                        id: conversationsTab

                        objectName: "conversationsTab"
                        width: implicitWidth
                        text: qsTr("Conversations")
                        icon.source: Theme.icon("messages")

                        FocusRing {
                            shown: conversationsTab.visualFocus
                        }
                    }

                    TabButton {
                        id: scheduledTab

                        objectName: "scheduledTab"
                        width: implicitWidth
                        text: qsTr("Scheduled")
                        icon.source: Theme.icon("clock")

                        FocusRing {
                            shown: scheduledTab.visualFocus
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ToolButton {
                    id: newButton

                    objectName: "newMessageButton"
                    text: qsTr("New message")
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

                    objectName: "importArchiveButton"
                    text: qsTr("Read an archive file")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("upload")
                    enabled: !root.area.busy
                    Accessible.name: importButton.text
                    ToolTip.visible: importButton.hovered
                    ToolTip.text: importButton.text
                    onClicked: importDialog.open()

                    FocusRing {
                        shown: importButton.visualFocus
                    }
                }

                ToolButton {
                    id: exportAllButton

                    objectName: "exportAllMessagesButton"
                    text: qsTr("Export all messages")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("download")
                    enabled: root.area.conversations.totalCount > 0 && !root.area.busy
                    Accessible.name: exportAllButton.text
                    ToolTip.visible: exportAllButton.hovered
                    ToolTip.text: exportAllButton.text
                    onClicked: root.startExport(true)

                    FocusRing {
                        shown: exportAllButton.visualFocus
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
            listComponent: root.showingScheduled ? scheduledListComponent : conversationListComponent
            detailComponent: Component {
                Loader {
                    sourceComponent: root.area.composing ? composeComponent : root.showingScheduled ? scheduledHintComponent : conversationComponent
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
        id: conversationListComponent

        ConversationList {
            area: root.area
            emptyText: root.area.canEdit ? qsTr("No messages yet. Sync the phone to load them.") : root.emptyText
            onOpenRequested: listDetail.detailOpen = true
            onDeleteRequested: root.confirmDeleteConversation()
        }
    }

    Component {
        id: scheduledListComponent

        ScheduledList {
            area: root.area
            onEditRequested: luid => {
                if (root.area.editScheduled(luid))
                    listDetail.detailOpen = true;
            }
            onDeleteRequested: luid => root.confirmDeleteScheduled(luid)
        }
    }

    Component {
        id: conversationComponent

        ConversationView {
            area: root.area
            onDeleteRequested: luids => root.confirmDelete(luids)
            onDeleteConversationRequested: root.confirmDeleteConversation()
            onExportRequested: root.startExport(false)
            onSaveAttachmentRequested: (luid, position) => root.startSaveAttachment(luid, position)
            onSaveAllRequested: luid => root.startSaveAll(luid)
        }
    }

    Component {
        id: composeComponent

        ComposeView {
            area: root.area
            onCancelled: {
                root.area.composing = false;
                listDetail.detailOpen = false;
            }
            onDeleteRequested: luid => root.confirmDeleteScheduled(luid)
        }
    }

    Component {
        id: scheduledHintComponent

        EmptyState {
            text: root.area.scheduled.count > 0 ? qsTr("Choose a scheduled message to change it, or plan a new one.") : qsTr("No scheduled messages. The phone sends a scheduled message on its own at the time you choose.")
            actionText: root.area.canEdit ? qsTr("Plan a message") : ""
            actionIcon: "clock"
            onActionTriggered: root.startNew(true)
        }
    }

    Shortcut {
        sequences: [StandardKey.New]
        enabled: root.visible && root.area.canEdit && !root.area.composing
        onActivated: root.startNew(root.showingScheduled)
    }

    Connections {
        target: root.area

        function onHasPhoneChanged(): void {
            listDetail.detailOpen = false;
        }

        function onComposingChanged(): void {
            if (root.area.composing)
                listDetail.detailOpen = true;
        }
    }

    Dialog {
        id: deleteDialog

        objectName: "deleteMessagesDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: TypeScale.body * 28
        modal: true
        Overlay.modal: Scrim {}
        Material.elevation: 0
        title: root.pendingDeleteConversation ? qsTr("Delete the conversation with %1?").arg(root.area.currentTitle) : root.pendingDeleteScheduled ? qsTr("Delete the scheduled message?") : qsTr("Delete %n message(s)?", "", root.pendingDelete.length)

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
        onAccepted: {
            if (root.pendingDeleteConversation)
                root.area.removeCurrentConversation();
            else
                root.area.removeMessages(root.pendingDelete);
            if (root.pendingDeleteScheduled)
                root.area.composing = false;
        }

        Label {
            objectName: "deleteMessagesText"
            anchors.left: parent.left
            anchors.right: parent.right
            text: root.pendingDeleteScheduled ? qsTr("The phone does not send it then.") : qsTr("The phone deletes text messages only if its app allows it. Otherwise they stay on the phone and in this list.")
            wrapMode: Text.Wrap
        }
    }

    FileDialog {
        id: exportDialog

        objectName: "exportMessagesDialog"
        title: root.pendingExportAll ? qsTr("Export all messages") : qsTr("Export the conversation")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Table for spreadsheets (*.csv)"), qsTr("Web page (*.html)"), qsTr("Archive file (*.xml)")]
        defaultSuffix: exportDialog.selectedNameFilter.index === 2 ? "xml" : exportDialog.selectedNameFilter.index === 1 ? "html" : "csv"
        onAccepted: {
            const format = exportDialog.selectedNameFilter.index === 2 ? MessagesArea.Xml : exportDialog.selectedNameFilter.index === 1 ? MessagesArea.Html : MessagesArea.Csv;
            if (root.pendingExportAll)
                root.area.exportAll(exportDialog.selectedFile, format);
            else
                root.area.exportConversation(exportDialog.selectedFile, format);
        }
    }

    FileDialog {
        id: importDialog

        objectName: "importArchiveDialog"
        title: qsTr("Read an archive file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Archive file (*.xml)"), qsTr("All files (*)")]
        onAccepted: root.area.importArchive(importDialog.selectedFile)
    }

    FileDialog {
        id: attachmentDialog

        objectName: "saveAttachmentDialog"
        title: qsTr("Save the attachment")
        fileMode: FileDialog.SaveFile
        onAccepted: root.area.saveAttachment(root.pendingAttachmentLuid, root.pendingAttachmentPosition, attachmentDialog.selectedFile)
    }

    FolderDialog {
        id: folderDialog

        objectName: "saveAllAttachmentsDialog"
        title: qsTr("Save all attachments in a folder")
        onAccepted: root.area.saveAllAttachments(root.pendingAttachmentLuid, folderDialog.selectedFolder)
    }

    function startNew(scheduled: bool): void {
        root.area.composeNew();
        root.area.composer.scheduled = scheduled;
        listDetail.detailOpen = true;
    }

    function confirmDelete(luids: list<int>): void {
        if (luids.length === 0)
            return;
        root.pendingDelete = luids;
        root.pendingDeleteConversation = false;
        root.pendingDeleteScheduled = false;
        deleteDialog.open();
    }

    function confirmDeleteScheduled(luid: int): void {
        if (luid <= 0 || !root.area.canEdit)
            return;
        root.pendingDelete = [luid];
        root.pendingDeleteConversation = false;
        root.pendingDeleteScheduled = true;
        deleteDialog.open();
    }

    function confirmDeleteConversation(): void {
        if (!root.area.hasCurrent)
            return;
        root.pendingDelete = [];
        root.pendingDeleteConversation = true;
        root.pendingDeleteScheduled = false;
        deleteDialog.open();
    }

    function startExport(all: bool): void {
        root.pendingExportAll = all;
        exportDialog.open();
    }

    function startSaveAttachment(luid: int, position: int): void {
        root.pendingAttachmentLuid = luid;
        root.pendingAttachmentPosition = position;
        attachmentDialog.selectedFile = root.area.suggestedFileUrl(luid, position, attachmentDialog.currentFolder);
        attachmentDialog.open();
    }

    function startSaveAll(luid: int): void {
        root.pendingAttachmentLuid = luid;
        folderDialog.open();
    }
}
