pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    readonly property CallsArea area: AppController.calls
    required property string emptyText
    property list<int> pendingDelete: []
    property string pendingDeleteTitle: ""

    signal connectRequested

    objectName: "callsPage"

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
                    id: kindTabs

                    objectName: "callKindTabs"
                    currentIndex: root.area.list.kindFilter
                    Accessible.name: qsTr("Show calls")
                    onCurrentIndexChanged: root.area.list.kindFilter = root.kindAt(kindTabs.currentIndex)
                    Component.onCompleted: Theme.calmTabBar(kindTabs)

                    TabButton {
                        id: allTab

                        objectName: "allCallsTab"
                        width: implicitWidth
                        text: qsTr("All")

                        FocusRing {
                            shown: allTab.activeFocus
                        }
                    }

                    TabButton {
                        id: incomingTab

                        objectName: "incomingCallsTab"
                        width: implicitWidth
                        text: qsTr("Incoming")
                        icon.source: Theme.icon("phone-incoming")

                        FocusRing {
                            shown: incomingTab.activeFocus
                        }
                    }

                    TabButton {
                        id: outgoingTab

                        objectName: "outgoingCallsTab"
                        width: implicitWidth
                        text: qsTr("Outgoing")
                        icon.source: Theme.icon("phone-outgoing")

                        FocusRing {
                            shown: outgoingTab.activeFocus
                        }
                    }

                    TabButton {
                        id: missedTab

                        objectName: "missedCallsTab"
                        width: implicitWidth
                        text: qsTr("Missed")
                        icon.source: Theme.icon("phone-x")

                        FocusRing {
                            shown: missedTab.activeFocus
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ToolButton {
                    id: exportButton

                    objectName: "exportCallsButton"
                    text: qsTr("Export the calls in the list")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("download")
                    enabled: root.area.list.count > 0 && !root.area.busy
                    Accessible.name: exportButton.text
                    ToolTip.visible: exportButton.hovered
                    ToolTip.text: exportButton.text
                    onClicked: exportDialog.open()

                    FocusRing {
                        shown: exportButton.activeFocus
                    }
                }

                ToolButton {
                    id: deleteShownButton

                    objectName: "deleteShownCallsButton"
                    text: qsTr("Delete the calls in the list")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("trash")
                    enabled: root.area.canEdit && root.area.list.count > 0 && !root.area.busy
                    Accessible.name: deleteShownButton.text
                    ToolTip.visible: deleteShownButton.hovered
                    ToolTip.text: deleteShownButton.text
                    onClicked: root.confirmDeleteShown()

                    FocusRing {
                        shown: deleteShownButton.activeFocus
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.area.hasPhone
            color: Theme.outlineVariant
        }

        ListDetailView {
            id: listDetail

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.area.hasPhone
            listComponent: Component {
                CallList {
                    area: root.area
                    emptyText: root.area.canEdit ? qsTr("No calls yet. Sync the phone to load the call log.") : root.emptyText
                    onOpenRequested: listDetail.detailOpen = true
                    onDeleteRequested: id => root.confirmDelete(id)
                }
            }
            detailComponent: Component {
                CallDetail {
                    area: root.area
                    onDeleteRequested: id => root.confirmDelete(id)
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

    Dialog {
        id: deleteDialog

        objectName: "deleteCallsDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: TypeScale.body * 28
        modal: true
        Overlay.modal: Scrim {}
        Material.elevation: 0
        title: root.pendingDelete.length === 1 && root.pendingDeleteTitle.length > 0 ? qsTr("Delete the call with %1?").arg(root.pendingDeleteTitle) : qsTr("Delete %n call(s)?", "", root.pendingDelete.length)

        footer: DialogButtonBox {
            DialogButton {
                id: confirmDeleteButton

                text: qsTr("Delete")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }

            DialogButton {
                id: cancelDeleteButton

                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onOpened: cancelDeleteButton.forceActiveFocus(Qt.PopupFocusReason)
        onAccepted: root.area.removeCalls(root.pendingDelete)
        Component.onCompleted: Theme.calmPopup(deleteDialog)

        Label {
            objectName: "deleteCallsText"
            anchors.left: parent.left
            anchors.right: parent.right
            text: qsTr("The phone deletes the call(s) only if its app allows it. Otherwise they stay on the phone and in this list.", "", root.pendingDelete.length)
            wrapMode: Text.Wrap
        }
    }

    FileDialog {
        id: exportDialog

        objectName: "exportCallsDialog"
        title: qsTr("Export calls")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV table (*.csv)")]
        defaultSuffix: "csv"
        onAccepted: root.area.exportCalls(exportDialog.selectedFile)
    }

    function kindAt(index: int): int {
        switch (index) {
        case 1:
            return CallListModel.Incoming;
        case 2:
            return CallListModel.Outgoing;
        case 3:
            return CallListModel.Missed;
        default:
            return CallListModel.All;
        }
    }

    function confirmDelete(id: int): void {
        if (id <= 0 || !root.area.canEdit)
            return;
        root.pendingDelete = [id];
        root.pendingDeleteTitle = root.area.currentId === id ? root.area.currentTitle : "";
        deleteDialog.open();
    }

    function confirmDeleteShown(): void {
        if (!root.area.canEdit || root.area.list.count === 0)
            return;
        root.pendingDelete = root.area.shownIds();
        root.pendingDeleteTitle = "";
        deleteDialog.open();
    }
}
