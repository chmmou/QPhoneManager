pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property MessagesArea area

    signal deleteRequested(list<int> luids)
    signal deleteConversationRequested
    signal exportRequested
    signal saveAttachmentRequested(int luid, int position)
    signal saveAllRequested(int luid)

    EmptyState {
        objectName: "conversationEmpty"
        anchors.fill: parent
        visible: !root.area.hasCurrent
        text: qsTr("Select a conversation in the list.")
    }

    ColumnLayout {
        objectName: "conversationView"
        anchors.fill: parent
        visible: root.area.hasCurrent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            Layout.margins: Theme.gridUnit * 4
            padding: Theme.gridUnit * 4
            background: Rectangle {
                color: Theme.surfaceContainer
                radius: Theme.cardRadius
            }

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit * 3

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit

                    Label {
                        objectName: "conversationTitle"
                        Layout.fillWidth: true
                        text: root.area.currentTitle
                        font.pixelSize: TypeScale.title
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.Heading
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.area.currentAddress.length > 0 && root.area.currentAddress !== root.area.currentTitle
                        text: root.area.currentAddress
                        color: Theme.foregroundOnSurfaceVariant
                        wrapMode: Text.Wrap
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    Button {
                        id: replyButton

                        objectName: "replyButton"
                        text: qsTr("Reply")
                        icon.source: Theme.icon("arrow-back-up")
                        enabled: root.area.canEdit && root.area.currentWritable
                        onClicked: root.area.replyToCurrent()

                        FocusRing {
                            shown: replyButton.visualFocus
                        }
                    }

                    Button {
                        id: readButton

                        objectName: "markReadButton"
                        text: qsTr("Mark as read")
                        icon.source: Theme.icon("check")
                        flat: true
                        enabled: root.area.canEdit && root.area.currentUnread > 0 && !root.area.busy
                        onClicked: root.area.markCurrentRead()

                        FocusRing {
                            shown: readButton.visualFocus
                        }
                    }

                    Button {
                        id: archiveButton

                        objectName: "archiveConversationButton"
                        text: qsTr("Archive")
                        icon.source: Theme.icon("archive")
                        flat: true
                        onClicked: root.area.archiveCurrentConversation()

                        FocusRing {
                            shown: archiveButton.visualFocus
                        }
                    }

                    Button {
                        id: exportButton

                        objectName: "exportConversationButton"
                        text: qsTr("Export")
                        icon.source: Theme.icon("download")
                        flat: true
                        enabled: !root.area.busy
                        onClicked: root.exportRequested()

                        FocusRing {
                            shown: exportButton.visualFocus
                        }
                    }

                    Button {
                        id: deleteButton

                        objectName: "deleteConversationButton"
                        text: qsTr("Delete")
                        icon.source: Theme.icon("trash")
                        flat: true
                        enabled: !root.area.busy
                        onClicked: root.deleteConversationRequested()

                        FocusRing {
                            shown: deleteButton.visualFocus
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list

                objectName: "messageList"
                anchors.fill: parent
                // The list scrolls through far more messages than fit.
                clip: true
                model: root.area.messages
                activeFocusOnTab: true
                keyNavigationEnabled: true
                highlightMoveDuration: 0
                spacing: Theme.gridUnit * 2
                bottomMargin: Theme.gridUnit * 4
                section.property: "section"
                section.criteria: ViewSection.FullString
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Messages")
                ScrollBar.vertical: ScrollBar {}

                section.delegate: Label {
                    required property string section

                    width: ListView.view.width
                    topPadding: Theme.gridUnit * 2
                    text: section
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: TypeScale.hint
                    font.bold: true
                    color: Theme.foregroundOnSurfaceVariant
                    Accessible.role: Accessible.Heading
                }

                delegate: Item {
                    id: messageRow

                    required property int index
                    required property int luid
                    required property bool outgoing
                    required property string body
                    required property string subject
                    required property string timeText
                    required property string statusText
                    required property string simText
                    required property bool read
                    required property bool archived
                    required property bool onPhone
                    required property var attachments

                    width: ListView.view.width
                    implicitHeight: bubble.implicitHeight
                    Accessible.role: Accessible.ListItem
                    Accessible.name: messageRow.body.length > 0 ? messageRow.body : messageRow.subject
                    Accessible.description: [messageRow.outgoing ? qsTr("Sent") : qsTr("Received"), messageRow.timeText, messageRow.statusText, messageRow.archived ? qsTr("In the archive") : ""].filter(part => part.length > 0).join(", ")

                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onTapped: (point, button) => {
                            list.currentIndex = messageRow.index;
                            list.forceActiveFocus(Qt.MouseFocusReason);
                            if (button === Qt.RightButton)
                                messageMenu.popup();
                        }
                    }

                    Rectangle {
                        id: bubble

                        implicitWidth: Math.min(messageRow.width * 0.75, bubbleContent.implicitWidth + Theme.gridUnit * 6)
                        implicitHeight: bubbleContent.implicitHeight + Theme.gridUnit * 4
                        width: bubble.implicitWidth
                        height: bubble.implicitHeight
                        x: messageRow.outgoing ? messageRow.width - bubble.width - Theme.gridUnit * 4 : Theme.gridUnit * 4
                        radius: Theme.cardRadius
                        color: messageRow.outgoing ? Theme.primary : Theme.surfaceContainer
                        border.width: messageRow.ListView.isCurrentItem && list.activeFocus ? 2 : 0
                        border.color: messageRow.outgoing ? Theme.foregroundOnSurface : Theme.primary

                        ColumnLayout {
                            id: bubbleContent

                            x: Theme.gridUnit * 3
                            y: Theme.gridUnit * 2
                            width: bubble.width - Theme.gridUnit * 6
                            spacing: Theme.gridUnit

                            Label {
                                Layout.fillWidth: true
                                visible: messageRow.subject.length > 0
                                text: messageRow.subject
                                font.bold: true
                                color: messageRow.outgoing ? Theme.foregroundOnPrimary : Theme.foregroundOnSurface
                                wrapMode: Text.Wrap
                            }

                            TextEdit {
                                Layout.fillWidth: true
                                Layout.maximumWidth: messageRow.width * 0.75 - Theme.gridUnit * 6
                                visible: messageRow.body.length > 0
                                text: messageRow.body
                                textFormat: TextEdit.PlainText
                                readOnly: true
                                selectByMouse: true
                                activeFocusOnPress: false
                                wrapMode: TextEdit.Wrap
                                font.pixelSize: TypeScale.body
                                color: messageRow.outgoing ? Theme.foregroundOnPrimary : Theme.foregroundOnSurface
                                selectionColor: Theme.surfaceVariant
                                selectedTextColor: Theme.foregroundOnSurface
                                Accessible.ignored: true
                            }

                            Repeater {
                                model: messageRow.attachments

                                delegate: RowLayout {
                                    id: partRow

                                    required property var modelData

                                    readonly property int position: partRow.modelData.position
                                    readonly property string name: partRow.modelData.name
                                    readonly property string sizeText: partRow.modelData.sizeText
                                    readonly property bool available: partRow.modelData.available
                                    readonly property string imageUrl: partRow.modelData.imageUrl

                                    Layout.fillWidth: true
                                    spacing: Theme.gridUnit * 2

                                    Image {
                                        Layout.preferredWidth: TypeScale.body * 4
                                        Layout.preferredHeight: TypeScale.body * 4
                                        visible: partRow.imageUrl.length > 0
                                        source: partRow.imageUrl
                                        sourceSize.width: TypeScale.body * 8
                                        sourceSize.height: TypeScale.body * 8
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        Accessible.ignored: true
                                    }

                                    TintedIcon {
                                        Layout.preferredWidth: Theme.listIconSize
                                        Layout.preferredHeight: Theme.listIconSize
                                        visible: partRow.imageUrl.length === 0
                                        name: "file"
                                        color: messageRow.outgoing ? Theme.foregroundOnPrimary : Theme.foregroundOnSurfaceVariant
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0

                                        Label {
                                            Layout.fillWidth: true
                                            text: partRow.name
                                            color: messageRow.outgoing ? Theme.foregroundOnPrimary : Theme.foregroundOnSurface
                                            elide: Text.ElideMiddle
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: partRow.available ? partRow.sizeText : qsTr("%1, stayed on the phone").arg(partRow.sizeText)
                                            font.pixelSize: TypeScale.hint
                                            color: messageRow.outgoing ? Theme.foregroundOnPrimary : Theme.foregroundOnSurfaceVariant
                                        }
                                    }

                                    ToolButton {
                                        id: savePartButton

                                        text: qsTr("Save %1").arg(partRow.name)
                                        display: AbstractButton.IconOnly
                                        icon.source: Theme.icon("download")
                                        icon.color: messageRow.outgoing ? Theme.foregroundOnPrimary : Theme.foregroundOnSurface
                                        enabled: partRow.available
                                        Accessible.name: savePartButton.text
                                        ToolTip.visible: savePartButton.hovered
                                        ToolTip.text: savePartButton.text
                                        onClicked: root.saveAttachmentRequested(messageRow.luid, partRow.position)

                                        FocusRing {
                                            shown: savePartButton.visualFocus
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignRight
                                spacing: Theme.gridUnit

                                TintedIcon {
                                    Layout.preferredWidth: TypeScale.hint
                                    Layout.preferredHeight: TypeScale.hint
                                    visible: messageRow.archived
                                    name: "archive"
                                    color: messageRow.outgoing ? Theme.foregroundOnPrimary : Theme.foregroundOnSurfaceVariant
                                }

                                Label {
                                    text: [messageRow.timeText, messageRow.simText, messageRow.statusText, messageRow.read ? "" : qsTr("Unread")].filter(part => part.length > 0).join(", ")
                                    font.pixelSize: TypeScale.hint
                                    font.bold: !messageRow.read
                                    color: messageRow.outgoing ? Theme.foregroundOnPrimary : Theme.foregroundOnSurfaceVariant
                                }
                            }
                        }
                    }
                }

                onCountChanged: list.positionViewAtEnd()
                Component.onCompleted: {
                    list.currentIndex = list.count - 1;
                    list.positionViewAtEnd();
                }
                Keys.onDeletePressed: {
                    const luid = root.area.messages.luidAt(list.currentIndex);
                    if (luid > 0)
                        root.deleteRequested([luid]);
                }
                Keys.onMenuPressed: messageMenu.popup(list.currentItem)
            }

            FocusRing {
                shown: list.activeFocus
            }
        }
    }

    Menu {
        id: messageMenu

        objectName: "messageMenu"
        Component.onCompleted: Theme.calmPopup(messageMenu)

        MenuItem {
            text: root.area.messages.archivedAt(list.currentIndex) ? qsTr("Take out of the archive") : qsTr("Keep in the archive")
            onTriggered: root.area.archive([root.area.messages.luidAt(list.currentIndex)], !root.area.messages.archivedAt(list.currentIndex))
        }

        MenuItem {
            text: qsTr("Mark as read")
            enabled: root.area.canEdit && root.area.messages.unreadAt(list.currentIndex)
            onTriggered: root.area.markRead([root.area.messages.luidAt(list.currentIndex)], true)
        }

        MenuItem {
            text: qsTr("Save all attachments")
            enabled: root.area.messages.savableAttachmentsAt(list.currentIndex) > 0
            onTriggered: root.saveAllRequested(root.area.messages.luidAt(list.currentIndex))
        }

        MenuItem {
            text: qsTr("Delete")
            onTriggered: root.deleteRequested([root.area.messages.luidAt(list.currentIndex)])
        }
    }
}
