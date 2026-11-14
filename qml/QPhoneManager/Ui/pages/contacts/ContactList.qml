pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property ContactsArea area
    required property string emptyText

    signal openRequested
    signal deleteRequested(int luid)

    ListView {
        id: list

        objectName: "contactList"
        anchors.fill: parent
        visible: list.count > 0
        // The list scrolls through far more contacts than fit.
        clip: true
        model: root.area.list
        // Starts at the contact shown, so that a list made anew keeps it.
        currentIndex: root.area.list.rowOf(root.area.currentLuid)
        activeFocusOnTab: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        reuseItems: true
        section.property: "section"
        section.criteria: ViewSection.FullString
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Contacts")
        ScrollBar.vertical: ThemedScrollBar {}

        section.delegate: Label {
            required property string section

            width: ListView.view.width
            leftPadding: Theme.gridUnit * 4
            topPadding: Theme.gridUnit * 2
            bottomPadding: Theme.gridUnit
            text: section
            font.pixelSize: TypeScale.hint
            font.bold: true
            color: Theme.primary
            Accessible.role: Accessible.Heading
        }

        // A plain row rather than a delegate control: the list is the one
        // focus stop, and the row shows the current entry by its colour and
        // an outline.
        delegate: Rectangle {
            id: contactRow

            required property int index
            required property int luid
            required property string displayName
            required property string primaryNumber
            required property string organization
            required property string photoUrl
            required property string initials

            width: ListView.view.width
            implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 4
            color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: contactRow.displayName
            Accessible.description: contactRow.primaryNumber

            TapHandler {
                onTapped: {
                    list.currentIndex = contactRow.index;
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
                    photoUrl: contactRow.photoUrl
                    initials: contactRow.initials
                    size: TypeScale.body * 2
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: contactRow.displayName
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: contactRow.primaryNumber.length > 0 ? contactRow.primaryNumber : contactRow.organization
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }
                }
            }

            CurrentMarker {
                current: contactRow.ListView.isCurrentItem
                viewFocused: list.activeFocus
            }
        }

        onCurrentIndexChanged: {
            const luid = root.area.list.luidAt(list.currentIndex);
            if (luid > 0)
                root.area.currentLuid = luid;
        }
        Keys.onReturnPressed: root.openRequested()
        Keys.onEnterPressed: root.openRequested()
        Keys.onDeletePressed: {
            if (root.area.currentLuid > 0)
                root.deleteRequested(root.area.currentLuid);
        }
    }

    FocusRing {
        shown: list.activeFocus
    }

    EmptyState {
        objectName: "contactsEmpty"
        anchors.fill: parent
        visible: list.count === 0
        text: root.area.list.totalCount > 0 ? qsTr("No contact matches the search or filter.") : root.emptyText
    }

    Connections {
        target: root.area

        function onCurrentLuidChanged(): void {
            list.currentIndex = root.area.list.rowOf(root.area.currentLuid);
        }
    }

    Connections {
        target: root.area.list

        function onCountChanged(): void {
            list.currentIndex = root.area.list.rowOf(root.area.currentLuid);
        }
    }
}
