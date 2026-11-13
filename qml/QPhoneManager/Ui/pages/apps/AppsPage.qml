pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    readonly property AppListModel apps: AppController.apps
    required property string emptyText
    property string currentPackage: ""
    readonly property bool hasCurrent: root.apps.canEdit && root.currentPackage.length > 0
    // The app whose removal waits for the answer in the dialog.
    property string pendingRemoval: ""
    // While the list is made anew, the view moves its current row on its own.
    property bool resetting: false

    signal connectRequested

    objectName: "appsPage"

    // The phone is asked for its apps only once they are shown.
    onVisibleChanged: root.loadIfShown()
    Component.onCompleted: root.loadIfShown()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: root.apps.canEdit
            padding: Theme.gridUnit * 2
            Material.background: Theme.surface

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit

                TabBar {
                    id: filterTabs

                    objectName: "appFilterTabs"
                    currentIndex: root.tabOf(root.apps.filter)
                    Accessible.name: qsTr("Show apps")
                    onCurrentIndexChanged: root.apps.filter = root.filterAt(filterTabs.currentIndex)
                    Component.onCompleted: Theme.calmTabBar(filterTabs)

                    TabButton {
                        id: userTab

                        objectName: "userAppsTab"
                        width: implicitWidth
                        text: qsTr("Your apps")

                        FocusRing {
                            shown: userTab.visualFocus
                        }
                    }

                    TabButton {
                        id: systemTab

                        objectName: "systemAppsTab"
                        width: implicitWidth
                        text: qsTr("System apps")

                        FocusRing {
                            shown: systemTab.visualFocus
                        }
                    }

                    TabButton {
                        id: allTab

                        objectName: "allAppsTab"
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
                    id: startButton

                    objectName: "startAppButton"
                    text: qsTr("Open on the phone")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("player-play")
                    enabled: root.hasCurrent
                    Accessible.name: startButton.text
                    ToolTip.visible: startButton.hovered
                    ToolTip.text: startButton.text
                    onClicked: root.startCurrent()

                    FocusRing {
                        shown: startButton.visualFocus
                    }
                }

                ToolButton {
                    id: backupButton

                    objectName: "backupAppButton"
                    text: qsTr("Save the APK on this computer")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("download")
                    enabled: root.hasCurrent && root.apps.canBackup(root.currentPackage) && !root.apps.transferActive
                    Accessible.name: backupButton.text
                    ToolTip.visible: backupButton.hovered
                    ToolTip.text: backupButton.text
                    onClicked: root.startBackup()

                    FocusRing {
                        shown: backupButton.visualFocus
                    }
                }

                ToolButton {
                    id: uninstallButton

                    objectName: "uninstallAppButton"
                    text: qsTr("Remove from the phone")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("trash")
                    enabled: root.hasCurrent
                    Accessible.name: uninstallButton.text
                    ToolTip.visible: uninstallButton.hovered
                    ToolTip.text: uninstallButton.text
                    onClicked: root.confirmUninstall()

                    FocusRing {
                        shown: uninstallButton.visualFocus
                    }
                }

                ToolSeparator {}

                ToolButton {
                    id: installButton

                    objectName: "installAppButton"
                    text: qsTr("Install an APK from this computer")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("upload")
                    enabled: !root.apps.transferActive
                    Accessible.name: installButton.text
                    ToolTip.visible: installButton.hovered
                    ToolTip.text: installButton.text
                    onClicked: installDialog.open()

                    FocusRing {
                        shown: installButton.visualFocus
                    }
                }

                ToolButton {
                    id: refreshButton

                    objectName: "refreshAppsButton"
                    text: qsTr("Load the apps again")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("refresh")
                    enabled: !root.apps.busy
                    Accessible.name: refreshButton.text
                    ToolTip.visible: refreshButton.hovered
                    ToolTip.text: refreshButton.text
                    onClicked: root.apps.refresh()

                    FocusRing {
                        shown: refreshButton.visualFocus
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.apps.canEdit
            color: Theme.outline
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.apps.canEdit

            ListView {
                id: appList

                objectName: "appList"
                anchors.fill: parent
                visible: appList.count > 0
                // The list scrolls through far more apps than fit.
                clip: true
                model: root.apps
                activeFocusOnTab: true
                keyNavigationEnabled: true
                highlightMoveDuration: 0
                reuseItems: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Apps")
                Accessible.description: qsTr("Enter opens the app on the phone, Delete removes it, the menu key shows more")
                ScrollBar.vertical: ScrollBar {}

                // A plain row rather than a delegate control: the list is the one
                // focus stop, and the row shows the current app by its colour.
                delegate: Rectangle {
                    id: appRow

                    required property int index
                    required property string packageName
                    required property string label
                    required property string version
                    required property string sizeText
                    required property string installedText
                    required property string iconUrl
                    required property string details

                    width: ListView.view.width
                    implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 3
                    color: appRow.ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: appRow.label
                    Accessible.description: appRow.details

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
                                id: appIcon

                                anchors.fill: parent
                                visible: appIcon.status === Image.Ready
                                source: appRow.iconUrl
                                sourceSize.width: Theme.listIconSize * 2
                                sourceSize.height: Theme.listIconSize * 2
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }

                            TintedIcon {
                                anchors.centerIn: parent
                                width: Theme.listIconSize
                                height: Theme.listIconSize
                                visible: !appIcon.visible
                                name: "apps"
                                color: Theme.foregroundOnSurfaceVariant
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                Layout.fillWidth: true
                                text: appRow.label
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: appRow.packageName
                                font.pixelSize: TypeScale.hint
                                color: Theme.foregroundOnSurfaceVariant
                                elide: Text.ElideMiddle
                            }
                        }

                        Label {
                            Layout.preferredWidth: TypeScale.body * 7
                            text: appRow.version
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.preferredWidth: TypeScale.body * 5
                            horizontalAlignment: Text.AlignRight
                            text: appRow.sizeText
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                        }

                        Label {
                            Layout.preferredWidth: TypeScale.body * 5
                            horizontalAlignment: Text.AlignRight
                            text: appRow.installedText
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onTapped: (point, button) => root.select(appRow.index, button === Qt.RightButton)
                        onDoubleTapped: root.startCurrent()
                    }
                }

                onCurrentIndexChanged: {
                    if (!root.resetting)
                        root.currentPackage = root.apps.packageAt(appList.currentIndex);
                }
                Keys.onPressed: event => root.handleKey(event)
            }

            FocusRing {
                objectName: "appListFocusRing"
                shown: appList.activeFocus
            }

            EmptyState {
                objectName: "appsEmpty"
                anchors.fill: parent
                visible: appList.count === 0 && !root.apps.busy
                text: root.apps.emptyText
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.apps.busy && appList.count === 0
                visible: running
            }
        }

        Pane {
            id: transferPane

            objectName: "appTransfer"
            Layout.fillWidth: true
            visible: root.apps.transferActive
            padding: Theme.gridUnit * 2
            Material.background: Theme.surfaceContainer

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit * 2

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit

                    Label {
                        objectName: "appTransferText"
                        Layout.fillWidth: true
                        text: root.apps.transferText
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    ProgressBar {
                        objectName: "appTransferProgress"
                        Layout.fillWidth: true
                        value: root.apps.transferProgress
                        Accessible.name: root.apps.transferText
                    }
                }

                Button {
                    id: cancelTransferButton

                    objectName: "cancelAppTransferButton"
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: root.apps.cancelTransfer()

                    FocusRing {
                        shown: cancelTransferButton.visualFocus
                    }
                }
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.apps.canEdit
            text: root.emptyText
            actionText: qsTr("Connect")
            actionIcon: "plug-connected"
            onActionTriggered: root.connectRequested()
        }
    }

    Menu {
        id: appMenu

        objectName: "appMenu"
        // Wide enough for the longest entry in both languages.
        width: TypeScale.body * 22
        Component.onCompleted: Theme.calmPopup(appMenu)

        MenuItem {
            text: qsTr("Open on the phone")
            enabled: root.hasCurrent
            onTriggered: root.startCurrent()
        }

        MenuItem {
            text: qsTr("Save the APK on this computer")
            enabled: root.hasCurrent && root.apps.canBackup(root.currentPackage) && !root.apps.transferActive
            onTriggered: root.startBackup()
        }

        MenuItem {
            text: qsTr("Remove from the phone")
            enabled: root.hasCurrent
            onTriggered: root.confirmUninstall()
        }
    }

    Dialog {
        id: uninstallDialog

        objectName: "uninstallAppDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: TypeScale.body * 28
        modal: true
        Overlay.modal: Scrim {}
        Material.elevation: 0
        title: qsTr("Remove %1?").arg(root.apps.labelOf(root.pendingRemoval))

        footer: DialogButtonBox {
            Button {
                id: confirmUninstallButton

                text: qsTr("Remove")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole

                FocusRing {
                    shown: confirmUninstallButton.visualFocus
                }
            }

            Button {
                id: cancelUninstallButton

                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

                FocusRing {
                    shown: cancelUninstallButton.visualFocus
                }
            }
        }

        // With the tab reason the button shows its focus ring right away. A
        // button that kept its focus from the last opening would keep the
        // reason of then, so it gives the focus up on closing.
        onOpened: cancelUninstallButton.forceActiveFocus(Qt.TabFocusReason)
        onClosed: cancelUninstallButton.focus = false
        onAccepted: root.apps.uninstall(root.pendingRemoval)
        Component.onCompleted: Theme.calmPopup(uninstallDialog)

        Label {
            objectName: "uninstallAppText"
            anchors.left: parent.left
            anchors.right: parent.right
            text: qsTr("The phone then asks you to confirm. After that, the app and its data are deleted from the phone.")
            wrapMode: Text.Wrap
        }
    }

    FileDialog {
        id: backupDialog

        objectName: "backupAppDialog"
        title: qsTr("Save the APK")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Android apps (*.apk)")]
        defaultSuffix: "apk"
        onAccepted: root.apps.backup(root.currentPackage, backupDialog.selectedFile)
    }

    FileDialog {
        id: installDialog

        objectName: "installAppDialog"
        title: qsTr("Install an APK on the phone")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Android apps (*.apk)")]
        onAccepted: root.apps.install(installDialog.selectedFile)
    }

    Connections {
        target: root.apps

        function onCanEditChanged(): void {
            root.currentPackage = "";
            root.loadIfShown();
        }

        function onModelAboutToBeReset(): void {
            root.resetting = true;
        }

        function onCountChanged(): void {
            const row = root.apps.rowOf(root.currentPackage);
            appList.currentIndex = row;
            if (row < 0)
                root.currentPackage = "";
            root.resetting = false;
        }
    }

    function loadIfShown(): void {
        if (root.visible && root.apps.canEdit)
            root.apps.load();
    }

    function tabOf(filter: int): int {
        switch (filter) {
        case AppListModel.System:
            return 1;
        case AppListModel.All:
            return 2;
        default:
            return 0;
        }
    }

    function filterAt(index: int): int {
        switch (index) {
        case 1:
            return AppListModel.System;
        case 2:
            return AppListModel.All;
        default:
            return AppListModel.User;
        }
    }

    function select(row: int, withMenu: bool): void {
        appList.currentIndex = row;
        appList.forceActiveFocus(Qt.MouseFocusReason);
        if (withMenu)
            appMenu.popup();
    }

    function startCurrent(): void {
        if (root.hasCurrent)
            root.apps.start(root.currentPackage);
    }

    function startBackup(): void {
        if (root.hasCurrent && root.apps.canBackup(root.currentPackage) && !root.apps.transferActive)
            backupDialog.open();
    }

    function confirmUninstall(): void {
        if (!root.hasCurrent)
            return;
        root.pendingRemoval = root.currentPackage;
        uninstallDialog.open();
    }

    function showMenu(): void {
        const item = appList.currentItem;
        if (item)
            appMenu.popup(item, item.width / 2, item.height / 2);
        else
            appMenu.popup();
    }

    function handleKey(event: KeyEvent): void {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            root.startCurrent();
        else if (event.key === Qt.Key_Delete)
            root.confirmUninstall();
        else if (event.key === Qt.Key_Menu)
            root.showMenu();
        else
            return;
        event.accepted = true;
    }
}
