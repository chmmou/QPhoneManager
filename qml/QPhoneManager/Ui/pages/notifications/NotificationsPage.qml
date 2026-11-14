pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    readonly property NotificationListModel notifications: AppController.notifications
    required property string emptyText
    readonly property bool hasCurrent: root.notifications.canEdit && root.currentKey.length > 0
    readonly property NotificationRow currentRow: notificationList.currentItem as NotificationRow
    readonly property string currentKey: root.currentRow ? root.currentRow.notificationKey : ""
    readonly property bool currentCanDismiss: root.hasCurrent && root.currentRow !== null && root.currentRow.canDismiss

    signal connectRequested

    objectName: "notificationsPage"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: AppController.connected
            padding: Theme.gridUnit * 2
            Material.background: Theme.surface

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit

                // Without access the phone reports no notifications, which says nothing about their number.
                Label {
                    objectName: "notificationCount"
                    Layout.fillWidth: true
                    text: root.notifications.accessMissing ? "" : qsTr("%n notification(s) on the phone", "", root.notifications.count)
                    color: Theme.foregroundOnSurfaceVariant
                    elide: Text.ElideRight
                }

                ToolButton {
                    id: openButton

                    objectName: "openNotificationButton"
                    text: qsTr("Open on the phone")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("external-link")
                    enabled: root.hasCurrent
                    Accessible.name: openButton.text
                    ToolTip.visible: openButton.hovered
                    ToolTip.text: openButton.text
                    onClicked: root.openCurrent()

                    FocusRing {
                        shown: openButton.visualFocus
                    }
                }

                ToolButton {
                    id: dismissButton

                    objectName: "dismissNotificationButton"
                    text: qsTr("Dismiss on the phone")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("x")
                    enabled: root.currentCanDismiss
                    Accessible.name: dismissButton.text
                    ToolTip.visible: dismissButton.hovered
                    ToolTip.text: dismissButton.text
                    onClicked: root.dismissCurrent()

                    FocusRing {
                        shown: dismissButton.visualFocus
                    }
                }

                ToolSeparator {}

                ToolButton {
                    id: refreshButton

                    objectName: "refreshNotificationsButton"
                    text: qsTr("Load again")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("refresh")
                    Accessible.name: refreshButton.text
                    ToolTip.visible: refreshButton.hovered
                    ToolTip.text: refreshButton.text
                    onClicked: root.notifications.refresh()

                    FocusRing {
                        shown: refreshButton.visualFocus
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: AppController.connected
            color: Theme.outline
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: notificationList.count > 0
            spacing: 0

            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: parent.width * 0.45

                ListView {
                    id: notificationList

                    objectName: "notificationList"
                    anchors.fill: parent
                    // The list scrolls through more notifications than fit.
                    clip: true
                    model: root.notifications
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    highlightMoveDuration: 0
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Notifications")
                    Accessible.description: qsTr("Enter opens the notification on the phone, Delete dismisses it there")
                    ScrollBar.vertical: ThemedScrollBar {}

                    delegate: NotificationRow {
                        width: ListView.view.width
                        onTapped: row => root.select(row)
                        onActivated: root.openCurrent()
                    }

                    Keys.onPressed: event => root.handleKey(event)
                }

                FocusRing {
                    objectName: "notificationListFocusRing"
                    shown: notificationList.activeFocus
                }
            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: Theme.outline
            }

            ThemedScrollView {
                id: detailView

                objectName: "notificationDetail"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: detailView.availableWidth - detailView.ScrollBar.vertical.width

                ColumnLayout {
                    width: detailView.contentWidth
                    spacing: Theme.gridUnit * 3

                    Item {
                        Layout.preferredHeight: Theme.gridUnit
                    }

                    Label {
                        objectName: "notificationDetailApp"
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.gridUnit * 4
                        Layout.rightMargin: Theme.gridUnit * 4
                        text: root.currentRow ? qsTr("%1, %2").arg(root.currentRow.app).arg(root.currentRow.timeText) : ""
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    Label {
                        objectName: "notificationDetailTitle"
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.gridUnit * 4
                        Layout.rightMargin: Theme.gridUnit * 4
                        text: root.currentRow ? root.currentRow.title : qsTr("Select a notification in the list.")
                        font.pixelSize: TypeScale.section
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }

                    Label {
                        objectName: "notificationDetailText"
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.gridUnit * 4
                        Layout.rightMargin: Theme.gridUnit * 4
                        visible: text.length > 0
                        text: root.currentRow ? root.currentRow.text : ""
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }

                    Image {
                        id: detailImage

                        objectName: "notificationDetailImage"
                        Layout.leftMargin: Theme.gridUnit * 4
                        Layout.preferredWidth: Math.min(detailView.availableWidth - Theme.gridUnit * 8, TypeScale.body * 20)
                        Layout.preferredHeight: detailImage.Layout.preferredWidth * 0.75
                        visible: detailImage.source.toString().length > 0 && detailImage.status === Image.Ready
                        source: root.currentRow ? root.currentRow.imageUrl : ""
                        sourceSize.width: TypeScale.body * 20
                        sourceSize.height: TypeScale.body * 15
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        horizontalAlignment: Image.AlignLeft
                        Accessible.role: Accessible.Graphic
                        Accessible.name: qsTr("Picture of the notification")
                    }

                    Flow {
                        objectName: "notificationActions"
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.gridUnit * 4
                        Layout.rightMargin: Theme.gridUnit * 4
                        spacing: Theme.gridUnit * 2
                        visible: actionRepeater.count > 0

                        Repeater {
                            id: actionRepeater

                            model: root.currentRow ? root.currentRow.actions : []

                            delegate: Button {
                                id: actionButton

                                required property string modelData

                                text: actionButton.modelData
                                enabled: root.hasCurrent
                                Accessible.description: qsTr("Runs this action on the phone")
                                onClicked: root.notifications.trigger(root.currentKey, actionButton.modelData)

                                FocusRing {
                                    shown: actionButton.visualFocus
                                }
                            }
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
        }

        EmptyState {
            objectName: "notificationsEmpty"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: notificationList.count === 0
            text: AppController.connected ? root.notifications.emptyText : root.emptyText
            actionText: !AppController.connected ? qsTr("Connect") : (root.notifications.accessMissing ? qsTr("Show the setting on the phone") : "")
            actionIcon: !AppController.connected ? "plug-connected" : "bell-off"
            onActionTriggered: {
                if (AppController.connected)
                    root.notifications.openAccessSettings();
                else
                    root.connectRequested();
            }
        }
    }

    component NotificationRow: Rectangle {
        id: row

        required property int index
        required property string notificationKey
        required property string app
        required property string title
        required property string text
        required property string timeText
        required property list<string> actions
        required property string imageUrl
        required property bool canDismiss

        signal tapped(int row)
        signal activated

        implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 3
        color: row.ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
        Accessible.role: Accessible.ListItem
        Accessible.name: row.title
        Accessible.description: qsTr("%1, %2. %3").arg(row.app).arg(row.timeText).arg(row.text)

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
                Layout.alignment: Qt.AlignTop

                Image {
                    id: rowImage

                    anchors.fill: parent
                    visible: rowImage.status === Image.Ready
                    source: row.imageUrl
                    sourceSize.width: Theme.listIconSize * 2
                    sourceSize.height: Theme.listIconSize * 2
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }

                TintedIcon {
                    anchors.centerIn: parent
                    width: Theme.listIconSize
                    height: Theme.listIconSize
                    visible: !rowImage.visible
                    name: "bell"
                    color: Theme.foregroundOnSurfaceVariant
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    Label {
                        Layout.fillWidth: true
                        text: row.app
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    Label {
                        text: row.timeText
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: row.title
                    font.bold: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }

                Label {
                    Layout.fillWidth: true
                    visible: row.text.length > 0
                    text: row.text
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
        }

        TapHandler {
            onTapped: row.tapped(row.index)
            onDoubleTapped: row.activated()
        }

        CurrentMarker {
            current: row.ListView.isCurrentItem
            viewFocused: row.ListView.view.activeFocus
        }
    }

    function select(row: int): void {
        notificationList.currentIndex = row;
        notificationList.forceActiveFocus(Qt.MouseFocusReason);
    }

    function openCurrent(): void {
        if (root.hasCurrent)
            root.notifications.trigger(root.currentKey, "");
    }

    function dismissCurrent(): void {
        if (root.currentCanDismiss)
            root.notifications.dismiss(root.currentKey);
    }

    function handleKey(event: KeyEvent): void {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            root.openCurrent();
        else if (event.key === Qt.Key_Delete)
            root.dismissCurrent();
        else
            return;
        event.accepted = true;
    }
}
