pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property MessagesArea area
    required property string emptyText

    signal openRequested
    signal deleteRequested

    ListView {
        id: list

        objectName: "conversationList"
        anchors.fill: parent
        visible: list.count > 0
        // The list scrolls through far more conversations than fit.
        clip: true
        model: root.area.conversations
        currentIndex: root.area.conversations.rowOf(root.area.currentThread)
        activeFocusOnTab: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        reuseItems: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Conversations")
        ScrollBar.vertical: ScrollBar {}

        // A plain row rather than a delegate control: the list is the one
        // focus stop, and the row shows the current entry by its colour.
        delegate: Rectangle {
            id: conversationRow

            required property int index
            required property string threadKey
            required property string title
            required property string snippet
            required property string dateText
            required property string initials
            required property int unread
            required property int archived
            required property bool hasAttachments

            width: ListView.view.width
            implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 4
            color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: conversationRow.title
            Accessible.description: conversationRow.unread > 0 ? qsTr("%n unread, %1", "", conversationRow.unread).arg(conversationRow.snippet) : conversationRow.snippet

            TapHandler {
                onTapped: {
                    list.currentIndex = conversationRow.index;
                    list.forceActiveFocus(Qt.MouseFocusReason);
                    root.openRequested();
                }
            }

            RowLayout {
                id: rowContent

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.gridUnit * 4
                anchors.rightMargin: Theme.gridUnit * 4
                spacing: Theme.gridUnit * 3

                ContactAvatar {
                    photoUrl: ""
                    initials: conversationRow.initials
                    size: TypeScale.body * 2
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        Label {
                            Layout.fillWidth: true
                            text: conversationRow.title
                            font.bold: conversationRow.unread > 0
                            elide: Text.ElideRight
                        }

                        Label {
                            text: conversationRow.dateText
                            font.pixelSize: TypeScale.hint
                            color: conversationRow.unread > 0 ? Theme.primary : Theme.foregroundOnSurfaceVariant
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit

                        TintedIcon {
                            Layout.preferredWidth: TypeScale.hint
                            Layout.preferredHeight: TypeScale.hint
                            visible: conversationRow.hasAttachments
                            name: "file"
                            color: Theme.foregroundOnSurfaceVariant
                        }

                        Label {
                            Layout.fillWidth: true
                            text: conversationRow.snippet
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                            elide: Text.ElideRight
                        }

                        TintedIcon {
                            Layout.preferredWidth: TypeScale.hint
                            Layout.preferredHeight: TypeScale.hint
                            visible: conversationRow.archived > 0
                            name: "archive"
                            color: Theme.foregroundOnSurfaceVariant
                        }

                        Rectangle {
                            visible: conversationRow.unread > 0
                            Layout.preferredWidth: Math.max(unreadLabel.implicitWidth + Theme.gridUnit * 2, unreadLabel.implicitHeight)
                            Layout.preferredHeight: unreadLabel.implicitHeight
                            radius: height / 2
                            color: Theme.primary

                            Label {
                                id: unreadLabel

                                anchors.centerIn: parent
                                text: String(conversationRow.unread)
                                font.pixelSize: TypeScale.hint
                                color: Theme.foregroundOnPrimary
                            }
                        }
                    }
                }
            }
        }

        onCurrentIndexChanged: {
            const key = root.area.conversations.threadKeyAt(list.currentIndex);
            if (key.length > 0 && key !== root.area.currentThread)
                root.area.currentThread = key;
        }
        Keys.onReturnPressed: root.openRequested()
        Keys.onEnterPressed: root.openRequested()
        Keys.onDeletePressed: {
            if (root.area.hasCurrent)
                root.deleteRequested();
        }
    }

    FocusRing {
        shown: list.activeFocus
    }

    EmptyState {
        objectName: "conversationsEmpty"
        anchors.fill: parent
        visible: list.count === 0
        text: root.area.conversations.totalCount > 0 ? qsTr("No conversation matches the search.") : root.emptyText
    }

    Connections {
        target: root.area

        function onCurrentThreadChanged(): void {
            list.currentIndex = root.area.conversations.rowOf(root.area.currentThread);
        }
    }

    Connections {
        target: root.area.conversations

        function onCountChanged(): void {
            list.currentIndex = root.area.conversations.rowOf(root.area.currentThread);
        }
    }
}
