pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    readonly property TasksArea area: AppController.tasks
    required property string emptyText
    property bool editing: false
    property int pendingDelete: 0

    signal connectRequested

    objectName: "tasksPage"

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
                    id: filterTabs

                    objectName: "taskFilterTabs"
                    currentIndex: root.area.list.filterIndex
                    Accessible.name: qsTr("Show tasks")
                    onCurrentIndexChanged: root.area.list.filterIndex = filterTabs.currentIndex

                    TabButton {
                        id: openTab

                        objectName: "openTasksTab"
                        width: implicitWidth
                        text: qsTr("Open")

                        FocusRing {
                            shown: openTab.visualFocus
                        }
                    }

                    TabButton {
                        id: doneTab

                        objectName: "doneTasksTab"
                        width: implicitWidth
                        text: qsTr("Done")

                        FocusRing {
                            shown: doneTab.visualFocus
                        }
                    }

                    TabButton {
                        id: allTab

                        objectName: "allTasksTab"
                        width: implicitWidth
                        text: qsTr("All")

                        FocusRing {
                            shown: allTab.visualFocus
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ToolButton {
                    id: newButton

                    objectName: "newTaskButton"
                    text: qsTr("New task")
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

                    objectName: "importTasksButton"
                    text: qsTr("Import tasks")
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

                    objectName: "exportTasksButton"
                    text: qsTr("Export tasks")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("download")
                    enabled: root.area.list.totalCount > 0 && !root.area.busy
                    Accessible.name: exportButton.text
                    ToolTip.visible: exportButton.hovered
                    ToolTip.text: exportButton.text
                    onClicked: exportDialog.open()

                    FocusRing {
                        shown: exportButton.visualFocus
                    }
                }

                ToolButton {
                    id: categoriesButton

                    objectName: "taskCategoriesButton"
                    text: qsTr("Task categories")
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
                TaskList {
                    area: root.area
                    emptyText: root.area.canEdit ? qsTr("No tasks yet. Sync the phone or add a task.") : root.emptyText
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
            text: qsTr("Select a task in the list, or add a new one.")
        }
    }

    Component {
        id: formComponent

        TaskForm {
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

        objectName: "deleteTaskDialog"
        title: qsTr("Delete task %1?").arg(root.area.list.titleOf(root.pendingDelete))
        message: qsTr("The task is deleted on the phone and on this computer.")
        onAccepted: {
            root.area.removeTask(root.pendingDelete);
            root.editing = false;
        }
    }

    VersionConflictDialog {
        id: conflictDialog

        objectName: "taskConflictDialog"
        title: qsTr("The task was changed on the phone")
        explanation: qsTr("Someone changed this task on the phone after it was loaded here. Choose which version to keep.")
        ownText: root.area.editor.ownVersionText
        phoneText: root.area.editor.phoneVersionText
        onKeepOwnRequested: root.area.editor.keepOwnVersion()
        onTakePhoneRequested: root.area.editor.takePhoneVersion()
    }

    CategoriesDialog {
        id: categoriesDialog

        objectName: "taskCategoriesDialog"
        title: qsTr("Task categories")
        categories: root.area.categories
        colorChoices: root.area.colorChoices
        canEdit: root.area.canEdit
        onAddRequested: (name, colorIndex) => root.area.addCategory(name, colorIndex)
        onEditRequested: (row, name, colorIndex) => root.area.editCategory(row, name, colorIndex)
        onRemoveRequested: row => root.area.removeCategory(row)
    }

    FileDialog {
        id: importDialog

        objectName: "importTasksDialog"
        title: qsTr("Import tasks")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("iCalendar (*.ics *.ical *.vcs)"), qsTr("All files (*)")]
        onAccepted: root.area.importFrom(importDialog.selectedFile)
    }

    FileDialog {
        id: exportDialog

        objectName: "exportTasksDialog"
        title: qsTr("Export tasks")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("iCalendar (*.ics)")]
        defaultSuffix: "ics"
        onAccepted: root.area.exportTo(exportDialog.selectedFile)
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
