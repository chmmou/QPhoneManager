pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property NotesArea area
    required property string emptyText

    signal openRequested
    signal deleteRequested(int luid)

    ListView {
        id: list

        objectName: "noteList"
        anchors.fill: parent
        visible: list.count > 0
        // The list scrolls through more notes than fit.
        clip: true
        model: root.area.list
        currentIndex: root.area.list.rowOf(root.area.currentLuid)
        activeFocusOnTab: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Notes")
        ScrollBar.vertical: ThemedScrollBar {}

        // A plain row rather than a delegate control: the list is the one
        // focus stop, and the row shows the current entry by its colour and
        // an outline.
        delegate: Rectangle {
            id: noteRow

            required property int index
            required property int luid
            required property string title
            required property string preview
            required property string modifiedText
            required property string categoriesText

            width: ListView.view.width
            implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 4
            color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: noteRow.title
            Accessible.description: noteRow.modifiedText

            TapHandler {
                onTapped: {
                    list.currentIndex = noteRow.index;
                    list.forceActiveFocus(Qt.MouseFocusReason);
                    root.openRequested();
                }
            }

            ColumnLayout {
                id: rowContent

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.gridUnit * 4
                anchors.rightMargin: Theme.gridUnit * 4
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    Label {
                        Layout.fillWidth: true
                        text: noteRow.title
                        elide: Text.ElideRight
                    }

                    Label {
                        text: noteRow.modifiedText
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: noteRow.preview
                    font.pixelSize: TypeScale.hint
                    color: Theme.foregroundOnSurfaceVariant
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: noteRow.categoriesText
                    font.pixelSize: TypeScale.hint
                    color: Theme.primary
                    elide: Text.ElideRight
                }
            }

            CurrentMarker {
                current: noteRow.ListView.isCurrentItem
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
        objectName: "notesEmpty"
        anchors.fill: parent
        visible: list.count === 0
        text: root.area.list.totalCount > 0 ? qsTr("No note matches the search or filter.") : root.emptyText
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
