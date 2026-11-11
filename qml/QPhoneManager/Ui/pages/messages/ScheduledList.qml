pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property MessagesArea area

    signal editRequested(int luid)
    signal deleteRequested(int luid)

    ListView {
        id: list

        objectName: "scheduledList"
        anchors.fill: parent
        visible: list.count > 0
        // The list scrolls when there are more messages than fit.
        clip: true
        model: root.area.scheduled
        activeFocusOnTab: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Scheduled messages")
        ScrollBar.vertical: ScrollBar {}

        // A plain row rather than a delegate control: the list is the one
        // focus stop, and the row shows the current entry by its colour.
        delegate: Rectangle {
            id: scheduledRow

            required property int index
            required property int luid
            required property string recipientsText
            required property string body
            required property string whenText
            required property string statusText
            required property bool failed

            width: ListView.view.width
            implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 4
            color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: scheduledRow.recipientsText
            Accessible.description: [scheduledRow.whenText, scheduledRow.statusText, scheduledRow.body].join(", ")

            TapHandler {
                onTapped: {
                    list.currentIndex = scheduledRow.index;
                    list.forceActiveFocus(Qt.MouseFocusReason);
                    root.editRequested(scheduledRow.luid);
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

                TintedIcon {
                    Layout.preferredWidth: Theme.listIconSize
                    Layout.preferredHeight: Theme.listIconSize
                    name: "clock"
                    color: scheduledRow.failed ? Theme.error : Theme.foregroundOnSurfaceVariant
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: scheduledRow.recipientsText
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: scheduledRow.body
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("%1, %2").arg(scheduledRow.whenText).arg(scheduledRow.statusText)
                        font.pixelSize: TypeScale.hint
                        color: scheduledRow.failed ? Theme.error : Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Keys.onReturnPressed: root.editCurrent()
        Keys.onEnterPressed: root.editCurrent()
        Keys.onDeletePressed: {
            const luid = root.luidAt(list.currentIndex);
            if (luid > 0)
                root.deleteRequested(luid);
        }
    }

    FocusRing {
        shown: list.activeFocus
    }

    EmptyState {
        objectName: "scheduledEmpty"
        anchors.fill: parent
        visible: list.count === 0
        text: qsTr("No scheduled messages.")
    }

    function luidAt(row: int): int {
        return root.area.scheduled.luidAt(row);
    }

    function editCurrent(): void {
        const luid = root.luidAt(list.currentIndex);
        if (luid > 0)
            root.editRequested(luid);
    }
}
