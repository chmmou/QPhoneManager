pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    readonly property NotesArea area: AppController.notes
    required property string emptyText
    property bool editing: false
    property int pendingDelete: 0

    signal connectRequested

    objectName: "notesPage"

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

                ComboBox {
                    id: categoryFilterBox

                    objectName: "noteCategoryFilter"
                    Layout.preferredWidth: TypeScale.body * 14
                    model: [qsTr("All categories")].concat(root.area.categories.names)
                    currentIndex: Math.max(0, root.area.categories.names.indexOf(root.area.list.categoryFilter) + 1)
                    Accessible.name: qsTr("Show notes of the category")
                    onActivated: index => root.area.list.categoryFilter = index > 0 ? root.area.categories.names[index - 1] : ""

                    FocusRing {
                        shown: categoryFilterBox.visualFocus
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ToolButton {
                    id: newButton

                    objectName: "newNoteButton"
                    text: qsTr("New note")
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
                    id: categoriesButton

                    objectName: "noteCategoriesButton"
                    text: qsTr("Note categories")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("filter")
                    Accessible.name: categoriesButton.text
                    ToolTip.visible: categoriesButton.hovered
                    ToolTip.text: categoriesButton.text
                    onClicked: categoriesDialog.open()

                    FocusRing {
                        shown: categoriesButton.visualFocus
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
                NoteList {
                    area: root.area
                    emptyText: root.area.canEdit ? qsTr("No notes yet. Sync the phone or add a note.") : root.emptyText
                    onOpenRequested: root.startEdit()
                    onDeleteRequested: luid => root.confirmDelete(luid)
                }
            }
            detailComponent: Component {
                Loader {
                    sourceComponent: root.editing ? formComponent : emptyDetail
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
        id: emptyDetail

        EmptyState {
            text: qsTr("Select a note in the list, or add a new one.")
        }
    }

    Component {
        id: formComponent

        NoteForm {
            area: root.area
            onFinished: root.editing = false
            onDeleteRequested: luid => root.confirmDelete(luid)
        }
    }

    Shortcut {
        sequences: [StandardKey.New]
        enabled: root.visible && root.area.canEdit
        onActivated: root.startNew()
    }

    Connections {
        target: root.area.editor

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

        // The form is the detail view; unsaved changes keep it on its item.
        function onCurrentLuidChanged(): void {
            if (root.area.currentLuid > 0 && !(root.editing && root.area.editor.modified))
                root.showCurrent();
        }
    }

    ConfirmDeleteDialog {
        id: deleteDialog

        objectName: "deleteNoteDialog"
        title: qsTr("Delete note %1?").arg(root.area.list.titleOfNote(root.pendingDelete))
        message: qsTr("The note is deleted on the phone and on this computer.")
        onAccepted: {
            root.area.removeNote(root.pendingDelete);
            root.editing = false;
        }
    }

    VersionConflictDialog {
        id: conflictDialog

        objectName: "noteConflictDialog"
        title: qsTr("The note was changed on the phone")
        explanation: qsTr("Someone changed this note on the phone after it was loaded here. Choose which version to keep.")
        ownText: root.area.editor.ownVersionText
        phoneText: root.area.editor.phoneVersionText
        onKeepOwnRequested: root.area.editor.keepOwnVersion()
        onTakePhoneRequested: root.area.editor.takePhoneVersion()
    }

    CategoriesDialog {
        id: categoriesDialog

        objectName: "noteCategoriesDialog"
        title: qsTr("Note categories")
        categories: root.area.categories
        colorChoices: root.area.colorChoices
        canEdit: root.area.canEdit
        onAddRequested: (name, colorIndex) => root.area.addCategory(name, colorIndex)
        onEditRequested: (row, name, colorIndex) => root.area.editCategory(row, name, colorIndex)
        onRemoveRequested: row => root.area.removeCategory(row)
    }

    function startNew(): void {
        root.area.startNew();
        root.editing = true;
        listDetail.detailOpen = true;
    }

    function showCurrent(): void {
        if (root.area.editor.load(root.area.currentLuid))
            root.editing = true;
    }

    function startEdit(): void {
        if (root.area.currentLuid > 0 && root.area.editor.load(root.area.currentLuid)) {
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
}
