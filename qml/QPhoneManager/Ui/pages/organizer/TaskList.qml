pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property TasksArea area
    required property string emptyText

    signal openRequested
    signal deleteRequested(int luid)

    ListView {
        id: list

        objectName: "taskList"
        anchors.fill: parent
        visible: list.count > 0
        // The list scrolls through more tasks than fit.
        clip: true
        model: root.area.list
        currentIndex: root.area.list.rowOf(root.area.currentLuid)
        activeFocusOnTab: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Tasks")
        ScrollBar.vertical: ScrollBar {}

        // A plain row rather than a delegate control: the list is the one
        // focus stop; space ticks the current task off.
        delegate: Rectangle {
            id: taskRow

            required property int index
            required property int luid
            required property string summary
            required property string dueText
            required property bool overdue
            required property bool completed
            required property int priority
            required property string categoriesText

            width: ListView.view.width
            implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 4
            color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: taskRow.summary
            Accessible.description: taskRow.completed ? qsTr("Done") : taskRow.dueText

            TapHandler {
                onTapped: {
                    list.currentIndex = taskRow.index;
                    list.forceActiveFocus(Qt.MouseFocusReason);
                    root.openRequested();
                }
            }

            RowLayout {
                id: rowContent

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.gridUnit * 2
                anchors.rightMargin: Theme.gridUnit * 4
                spacing: Theme.gridUnit * 2

                CheckBox {
                    id: doneBox

                    checked: taskRow.completed
                    enabled: root.area.canEdit
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Done: %1").arg(taskRow.summary)
                    onToggled: root.area.setCompleted(taskRow.luid, doneBox.checked)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: taskRow.summary
                        font.strikeout: taskRow.completed
                        color: taskRow.completed ? Theme.foregroundOnSurfaceVariant : Theme.foregroundOnSurface
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: [taskRow.dueText, taskRow.categoriesText].filter(part => part.length > 0).join(", ")
                        font.pixelSize: TypeScale.hint
                        color: taskRow.overdue ? Theme.error : Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }
                }

                Label {
                    visible: taskRow.priority === 0 && !taskRow.completed
                    text: qsTr("High")
                    font.pixelSize: TypeScale.hint
                    color: Theme.error
                }
            }
        }

        onCurrentIndexChanged: {
            const luid = root.area.list.luidAt(list.currentIndex);
            if (luid > 0)
                root.area.currentLuid = luid;
        }
        Keys.onReturnPressed: root.openRequested()
        Keys.onEnterPressed: root.openRequested()
        Keys.onSpacePressed: {
            const luid = root.area.list.luidAt(list.currentIndex);
            if (luid > 0)
                root.area.setCompleted(luid, !root.area.list.isCompleted(list.currentIndex));
        }
        Keys.onDeletePressed: {
            if (root.area.currentLuid > 0)
                root.deleteRequested(root.area.currentLuid);
        }
    }

    FocusRing {
        shown: list.activeFocus
    }

    EmptyState {
        objectName: "tasksEmpty"
        anchors.fill: parent
        visible: list.count === 0
        text: root.area.list.totalCount > 0 ? qsTr("No task matches the search or filter.") : root.emptyText
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
