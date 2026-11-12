pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    readonly property FileBrowserModel files: AppController.files
    readonly property TransferQueue transfers: AppController.transfers
    required property string emptyText
    property bool showGrid: true
    property int currentRow: -1
    readonly property string currentName: root.files.nameAt(root.currentRow)
    readonly property bool currentIsFolder: root.files.isFolderAt(root.currentRow)
    readonly property bool canChangeCurrent: root.files.canEdit && !root.files.atRoot && root.currentName.length > 0
    property string pendingDelete: ""
    // The folder left by going up, chosen again once the list is there.
    property string pendingSelection: ""

    signal connectRequested

    objectName: "filesPage"

    // The phone is asked for its files only once they are shown.
    onVisibleChanged: root.loadIfShown()
    Component.onCompleted: root.loadIfShown()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: root.files.canEdit
            padding: Theme.gridUnit * 2
            Material.background: Theme.surface

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit

                ToolButton {
                    id: upButton

                    objectName: "folderUpButton"
                    text: qsTr("Up to the folder above")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("chevron-left")
                    enabled: !root.files.atRoot
                    Accessible.name: upButton.text
                    ToolTip.visible: upButton.hovered
                    ToolTip.text: upButton.text
                    onClicked: root.goUp()

                    FocusRing {
                        shown: upButton.visualFocus
                    }
                }

                Flickable {
                    id: pathBar

                    objectName: "pathBar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: crumbRow.implicitHeight
                    contentWidth: crumbRow.implicitWidth
                    // A deep path is wider than the bar; it scrolls to its end.
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    contentX: Math.max(0, pathBar.contentWidth - pathBar.width)
                    Accessible.role: Accessible.ToolBar
                    Accessible.name: qsTr("Path")

                    Row {
                        id: crumbRow

                        spacing: 0

                        Repeater {
                            model: root.files.crumbNames

                            delegate: Row {
                                id: crumb

                                required property int index
                                required property string modelData

                                TintedIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.listIconSize
                                    height: Theme.listIconSize
                                    visible: crumb.index > 0
                                    name: "chevron-right"
                                    color: Theme.foregroundOnSurfaceVariant
                                }

                                ToolButton {
                                    id: crumbButton

                                    objectName: "crumb" + crumb.index
                                    text: crumb.modelData
                                    font.bold: crumb.index === root.files.crumbNames.length - 1
                                    Accessible.name: crumbButton.text
                                    Accessible.description: qsTr("Go to this folder")
                                    onClicked: root.files.open(root.files.crumbPaths[crumb.index])

                                    FocusRing {
                                        shown: crumbButton.visualFocus
                                    }
                                }
                            }
                        }
                    }
                }

                ToolButton {
                    id: newFolderButton

                    objectName: "newFolderButton"
                    text: qsTr("New folder")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("folder-plus")
                    enabled: !root.files.atRoot
                    Accessible.name: newFolderButton.text
                    ToolTip.visible: newFolderButton.hovered
                    ToolTip.text: newFolderButton.text
                    onClicked: root.startNewFolder()

                    FocusRing {
                        shown: newFolderButton.visualFocus
                    }
                }

                ToolButton {
                    id: uploadButton

                    objectName: "uploadFilesButton"
                    text: qsTr("Copy files from this computer")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("upload")
                    enabled: !root.files.atRoot
                    Accessible.name: uploadButton.text
                    ToolTip.visible: uploadButton.hovered
                    ToolTip.text: uploadButton.text
                    onClicked: uploadMenu.popup(uploadButton, 0, uploadButton.height)

                    FocusRing {
                        shown: uploadButton.visualFocus
                    }
                }

                ToolButton {
                    id: downloadButton

                    objectName: "downloadFilesButton"
                    text: qsTr("Save on this computer")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("download")
                    enabled: root.canChangeCurrent
                    Accessible.name: downloadButton.text
                    ToolTip.visible: downloadButton.hovered
                    ToolTip.text: downloadButton.text
                    onClicked: downloadDialog.open()

                    FocusRing {
                        shown: downloadButton.visualFocus
                    }
                }

                ToolButton {
                    id: pasteButton

                    objectName: "pasteFilesButton"
                    text: qsTr("Paste here")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("clipboard")
                    enabled: root.files.canPaste && !root.files.atRoot
                    Accessible.name: pasteButton.text
                    ToolTip.visible: pasteButton.hovered
                    ToolTip.text: pasteButton.text
                    onClicked: root.files.paste()

                    FocusRing {
                        shown: pasteButton.visualFocus
                    }
                }

                ToolButton {
                    id: refreshButton

                    objectName: "refreshFilesButton"
                    text: qsTr("Load the folder again")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("refresh")
                    enabled: !root.files.busy
                    Accessible.name: refreshButton.text
                    ToolTip.visible: refreshButton.hovered
                    ToolTip.text: refreshButton.text
                    onClicked: root.files.refresh()

                    FocusRing {
                        shown: refreshButton.visualFocus
                    }
                }

                ToolButton {
                    id: viewButton

                    objectName: "fileViewButton"
                    text: root.showGrid ? qsTr("Show as a list") : qsTr("Show as a grid")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon(root.showGrid ? "list" : "layout-grid")
                    Accessible.name: viewButton.text
                    ToolTip.visible: viewButton.hovered
                    ToolTip.text: viewButton.text
                    onClicked: root.showGrid = !root.showGrid

                    FocusRing {
                        shown: viewButton.visualFocus
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.files.canEdit
            color: Theme.outline
        }

        Item {
            id: content

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.files.canEdit

            GridView {
                id: grid

                objectName: "fileGrid"
                anchors.fill: parent
                anchors.margins: Theme.gridUnit * 2
                visible: root.showGrid && grid.count > 0
                // The grid scrolls through far more entries than fit.
                clip: true
                model: root.files
                cellWidth: TypeScale.body * 9
                cellHeight: TypeScale.body * 9
                currentIndex: root.currentRow
                activeFocusOnTab: root.showGrid
                keyNavigationEnabled: true
                highlightMoveDuration: 0
                reuseItems: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Files")
                Accessible.description: qsTr("Enter opens, Backspace goes up, F2 renames, Delete deletes, the menu key shows more")
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: tile

                    required property int index
                    required property string name
                    required property string displayName
                    required property bool isFolder
                    required property string details
                    required property string thumbnailUrl
                    required property string iconName

                    width: grid.cellWidth
                    height: grid.cellHeight
                    Accessible.role: Accessible.ListItem
                    Accessible.name: tile.displayName
                    Accessible.description: tile.details
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.gridUnit
                        radius: Theme.gridUnit * 2
                        color: tile.GridView.isCurrentItem ? Theme.surfaceVariant : "transparent"
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.gridUnit * 2
                        spacing: Theme.gridUnit

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Image {
                                id: thumbnail

                                anchors.fill: parent
                                visible: thumbnail.status === Image.Ready
                                source: tile.thumbnailUrl
                                sourceSize.width: TypeScale.body * 8
                                sourceSize.height: TypeScale.body * 8
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }

                            TintedIcon {
                                anchors.centerIn: parent
                                width: Theme.sidebarIconSize * 2
                                height: Theme.sidebarIconSize * 2
                                visible: !thumbnail.visible
                                name: tile.iconName
                                color: tile.isFolder ? Theme.primary : Theme.foregroundOnSurfaceVariant
                            }
                        }

                        // A name of one word would break at any letter when it is too
                        // long; cut in the middle it keeps its start and its extension.
                        // Text elides in the middle only on a single line.
                        Label {
                            id: tileName

                            readonly property bool oneWord: !tile.displayName.includes(" ")

                            Layout.fillWidth: true
                            text: tile.displayName
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: tileName.oneWord ? Text.NoWrap : Text.Wrap
                            maximumLineCount: 2
                            elide: tileName.oneWord ? Text.ElideMiddle : Text.ElideRight
                            font.pixelSize: TypeScale.hint
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onTapped: (point, button) => root.select(tile.index, grid, button === Qt.RightButton)
                        onDoubleTapped: root.files.openRow(tile.index)
                    }

                    DragHandler {
                        id: tileDrag

                        target: null
                        enabled: !tile.isFolder && !root.files.atRoot
                        onActiveChanged: {
                            if (tileDrag.active)
                                root.files.prepareDrag([tile.name]);
                            else
                                tile.Drag.active = false;
                        }
                    }

                    // The copy is made when the drag starts; it begins once it is there.
                    Connections {
                        target: root.files
                        enabled: tileDrag.active

                        function onDragReady(urls: list<url>): void {
                            tile.Drag.mimeData = {
                                "text/uri-list": urls.map(url => url.toString()).join("\r\n")
                            };
                            tile.Drag.active = true;
                        }
                    }
                }

                onCurrentIndexChanged: root.currentRow = grid.currentIndex
                Keys.onPressed: event => root.handleKey(event)
            }

            ListView {
                id: list

                objectName: "fileList"
                anchors.fill: parent
                visible: !root.showGrid && list.count > 0
                // The list scrolls through far more entries than fit.
                clip: true
                model: root.files
                currentIndex: root.currentRow
                activeFocusOnTab: !root.showGrid
                keyNavigationEnabled: true
                highlightMoveDuration: 0
                reuseItems: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Files")
                Accessible.description: grid.Accessible.description
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: row

                    required property int index
                    required property string name
                    required property string displayName
                    required property bool isFolder
                    required property string details
                    required property string thumbnailUrl
                    required property string iconName

                    width: ListView.view.width
                    implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 3
                    color: row.ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: row.displayName
                    Accessible.description: row.details
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction

                    RowLayout {
                        id: rowContent

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Theme.gridUnit * 4
                        anchors.rightMargin: Theme.gridUnit * 4
                        spacing: Theme.gridUnit * 3

                        Item {
                            Layout.preferredWidth: Theme.listIconSize * 2
                            Layout.preferredHeight: Theme.listIconSize * 2

                            Image {
                                id: smallThumbnail

                                anchors.fill: parent
                                visible: smallThumbnail.status === Image.Ready
                                source: row.thumbnailUrl
                                sourceSize.width: Theme.listIconSize * 4
                                sourceSize.height: Theme.listIconSize * 4
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }

                            TintedIcon {
                                anchors.centerIn: parent
                                width: Theme.listIconSize
                                height: Theme.listIconSize
                                visible: !smallThumbnail.visible
                                name: row.iconName
                                color: row.isFolder ? Theme.primary : Theme.foregroundOnSurfaceVariant
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: row.displayName
                            elide: Text.ElideMiddle
                        }

                        Label {
                            text: row.details
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onTapped: (point, button) => root.select(row.index, list, button === Qt.RightButton)
                        onDoubleTapped: root.files.openRow(row.index)
                    }

                    DragHandler {
                        id: rowDrag

                        target: null
                        enabled: !row.isFolder && !root.files.atRoot
                        onActiveChanged: {
                            if (rowDrag.active)
                                root.files.prepareDrag([row.name]);
                            else
                                row.Drag.active = false;
                        }
                    }

                    Connections {
                        target: root.files
                        enabled: rowDrag.active

                        function onDragReady(urls: list<url>): void {
                            row.Drag.mimeData = {
                                "text/uri-list": urls.map(url => url.toString()).join("\r\n")
                            };
                            row.Drag.active = true;
                        }
                    }
                }

                onCurrentIndexChanged: root.currentRow = list.currentIndex
                Keys.onPressed: event => root.handleKey(event)
            }

            FocusRing {
                shown: grid.activeFocus || list.activeFocus
            }

            EmptyState {
                objectName: "filesEmpty"
                anchors.fill: parent
                visible: root.files.count === 0 && !root.files.busy
                text: root.files.emptyText
                actionText: root.files.atRoot ? "" : qsTr("Copy files from this computer")
                actionIcon: "upload"
                onActionTriggered: uploadDialog.open()
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.files.busy && root.files.count === 0
                visible: running
            }

            Rectangle {
                anchors.fill: parent
                visible: dropArea.containsDrag
                color: "transparent"
                radius: Theme.cardRadius
                border.width: 2
                border.color: Theme.primary
            }

            DropArea {
                id: dropArea

                anchors.fill: parent
                enabled: !root.files.atRoot
                // Drags from this window carry the program's own copies, which
                // must not go back to the phone.
                onEntered: drag => {
                    if (drag.source !== null || !drag.hasUrls)
                        drag.accepted = false;
                }
                onDropped: drop => {
                    if (drop.hasUrls && root.files.upload(drop.urls))
                        drop.accept(Qt.CopyAction);
                }
            }
        }

        TransferPanel {
            Layout.fillWidth: true
            visible: root.transfers.count > 0
            transfers: root.transfers
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.files.canEdit
            text: root.emptyText
            actionText: qsTr("Connect")
            actionIcon: "plug-connected"
            onActionTriggered: root.connectRequested()
        }
    }

    Menu {
        id: uploadMenu

        objectName: "uploadMenu"

        MenuItem {
            text: qsTr("Copy files")
            onTriggered: uploadDialog.open()
        }

        MenuItem {
            text: qsTr("Copy a folder")
            onTriggered: uploadFolderDialog.open()
        }
    }

    Menu {
        id: entryMenu

        objectName: "fileMenu"
        // Wide enough for the longest entry in both languages.
        width: TypeScale.body * 18

        MenuItem {
            text: root.currentIsFolder ? qsTr("Open") : qsTr("Open on this computer")
            enabled: root.currentName.length > 0
            onTriggered: root.files.openRow(root.currentRow)
        }

        MenuItem {
            text: qsTr("Save on this computer")
            enabled: root.canChangeCurrent
            onTriggered: downloadDialog.open()
        }

        MenuItem {
            text: qsTr("Rename")
            enabled: root.canChangeCurrent
            onTriggered: root.startRename()
        }

        MenuItem {
            text: qsTr("Cut")
            enabled: root.canChangeCurrent
            onTriggered: root.files.cut([root.currentName])
        }

        MenuItem {
            text: qsTr("Paste here")
            enabled: root.files.canPaste && !root.files.atRoot
            onTriggered: root.files.paste()
        }

        MenuItem {
            text: qsTr("Delete")
            enabled: root.canChangeCurrent
            onTriggered: root.confirmDelete()
        }
    }

    FileNameDialog {
        id: nameDialog

        objectName: "fileNameDialog"
        model: root.files
        onNameChosen: name => {
            if (nameDialog.oldName.length > 0)
                root.files.rename(nameDialog.oldName, name);
            else
                root.files.mkdir(name);
        }
    }

    Dialog {
        id: deleteDialog

        objectName: "deleteFileDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: TypeScale.body * 28
        modal: true
        Overlay.modal: Scrim {}
        Material.elevation: 0
        title: qsTr("Delete %1?").arg(root.pendingDelete)

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

        onOpened: cancelDeleteButton.forceActiveFocus(Qt.TabFocusReason)
        onClosed: cancelDeleteButton.focus = false
        onAccepted: root.files.remove([root.pendingDelete])

        Label {
            objectName: "deleteFileText"
            anchors.left: parent.left
            anchors.right: parent.right
            text: root.files.isFolderAt(root.files.rowOf(root.pendingDelete)) ? qsTr("The folder is deleted on the phone with everything in it. This cannot be undone.") : qsTr("The file is deleted on the phone. This cannot be undone.")
            wrapMode: Text.Wrap
        }
    }

    FileConflictDialog {
        id: conflictDialog

        objectName: "fileConflictDialog"
        transfers: root.transfers
    }

    Connections {
        target: root.transfers

        function onConflictChanged(): void {
            if (root.transfers.conflictId !== 0 && !conflictDialog.opened)
                conflictDialog.open();
        }
    }

    Connections {
        target: root.files

        function onPathChanged(): void {
            root.currentRow = -1;
        }

        function onCanEditChanged(): void {
            root.loadIfShown();
        }

        function onCountChanged(): void {
            if (root.pendingSelection.length === 0)
                return;
            const row = root.files.rowOf(root.pendingSelection);
            if (row >= 0) {
                root.currentRow = row;
                root.pendingSelection = "";
            }
        }
    }

    FileDialog {
        id: uploadDialog

        objectName: "uploadFilesDialog"
        title: qsTr("Copy files to the phone")
        fileMode: FileDialog.OpenFiles
        onAccepted: root.files.upload(uploadDialog.selectedFiles)
    }

    FolderDialog {
        id: uploadFolderDialog

        objectName: "uploadFolderDialog"
        title: qsTr("Copy a folder to the phone")
        onAccepted: root.files.upload([uploadFolderDialog.selectedFolder])
    }

    FolderDialog {
        id: downloadDialog

        objectName: "downloadFilesDialog"
        title: qsTr("Save on this computer")
        onAccepted: root.files.download([root.currentName], downloadDialog.selectedFolder)
    }

    Shortcut {
        sequences: [StandardKey.New]
        enabled: root.visible && root.files.canEdit && !root.files.atRoot
        onActivated: root.startNewFolder()
    }

    function goUp(): void {
        const from = root.files.path;
        if (root.files.up())
            root.pendingSelection = from.substring(from.lastIndexOf("\\") + 1);
    }

    function loadIfShown(): void {
        if (root.visible && root.files.canEdit)
            root.files.load();
    }

    function startNewFolder(): void {
        if (!root.files.canEdit || root.files.atRoot)
            return;
        nameDialog.oldName = "";
        nameDialog.open();
    }

    function startRename(): void {
        if (!root.canChangeCurrent)
            return;
        nameDialog.oldName = root.currentName;
        nameDialog.open();
    }

    function confirmDelete(): void {
        if (!root.canChangeCurrent)
            return;
        root.pendingDelete = root.currentName;
        deleteDialog.open();
    }

    function select(row: int, view: Item, withMenu: bool): void {
        root.currentRow = row;
        view.forceActiveFocus(Qt.MouseFocusReason);
        if (withMenu)
            entryMenu.popup();
    }

    function showMenu(): void {
        const view = root.showGrid ? grid : list;
        const item = view.currentItem;
        if (item)
            entryMenu.popup(item, item.width / 2, item.height / 2);
        else
            entryMenu.popup();
    }

    function handleKey(event: KeyEvent): void {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.files.openRow(root.currentRow);
        } else if (event.key === Qt.Key_Backspace || (event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier))) {
            root.goUp();
        } else if (event.key === Qt.Key_Delete) {
            root.confirmDelete();
        } else if (event.key === Qt.Key_F2) {
            root.startRename();
        } else if (event.key === Qt.Key_Menu) {
            root.showMenu();
        } else if (event.matches(StandardKey.Cut)) {
            if (root.canChangeCurrent)
                root.files.cut([root.currentName]);
        } else if (event.matches(StandardKey.Paste)) {
            root.files.paste();
        } else {
            return;
        }
        event.accepted = true;
    }
}
