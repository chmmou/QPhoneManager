pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property CallsArea area
    required property string emptyText

    signal openRequested
    signal deleteRequested(int id)

    ListView {
        id: list

        objectName: "callList"
        anchors.fill: parent
        visible: list.count > 0
        // The list scrolls through far more calls than fit.
        clip: true
        model: root.area.list
        // Starts at the entry shown, so that a list made anew keeps it.
        currentIndex: root.area.list.rowOf(root.area.currentId)
        activeFocusOnTab: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        reuseItems: true
        section.property: "section"
        section.criteria: ViewSection.FullString
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Calls")
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
            id: callRow

            required property int index
            required property int callId
            required property string title
            required property string number
            required property string name
            required property int kind
            required property string kindText
            required property string timeText
            required property string durationText

            width: ListView.view.width
            implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 4
            color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: callRow.title
            Accessible.description: [callRow.kindText, callRow.timeText, callRow.durationText].filter(part => part.length > 0).join(", ")

            TapHandler {
                onTapped: {
                    list.currentIndex = callRow.index;
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

                TintedIcon {
                    Layout.preferredWidth: Theme.listIconSize
                    Layout.preferredHeight: Theme.listIconSize
                    name: callRow.kind === CallListModel.Missed ? "phone-x" : callRow.kind === CallListModel.Incoming ? "phone-incoming" : "phone-outgoing"
                    color: callRow.kind === CallListModel.Missed ? Theme.error : Theme.foregroundOnSurfaceVariant
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: callRow.title
                        color: callRow.kind === CallListModel.Missed ? Theme.error : Theme.foregroundOnSurface
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: callRow.name.length > 0 && callRow.number.length > 0 ? callRow.number : callRow.kindText
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }
                }

                ColumnLayout {
                    spacing: 0

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: callRow.timeText
                        font.pixelSize: TypeScale.hint
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        visible: callRow.durationText.length > 0
                        text: callRow.durationText
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                    }
                }
            }

            CurrentMarker {
                current: callRow.ListView.isCurrentItem
                viewFocused: list.activeFocus
            }
        }

        onCurrentIndexChanged: {
            const id = root.area.list.idAt(list.currentIndex);
            if (id > 0)
                root.area.currentId = id;
        }
        Keys.onReturnPressed: root.openRequested()
        Keys.onEnterPressed: root.openRequested()
        Keys.onDeletePressed: {
            if (root.area.currentId > 0)
                root.deleteRequested(root.area.currentId);
        }
    }

    FocusRing {
        shown: list.activeFocus
    }

    EmptyState {
        objectName: "callsEmpty"
        anchors.fill: parent
        visible: list.count === 0
        text: root.area.list.totalCount > 0 ? qsTr("No call matches the search or filter.") : root.emptyText
    }

    Connections {
        target: root.area

        function onCurrentIdChanged(): void {
            list.currentIndex = root.area.list.rowOf(root.area.currentId);
        }
    }

    Connections {
        target: root.area.list

        function onCountChanged(): void {
            list.currentIndex = root.area.list.rowOf(root.area.currentId);
        }
    }
}
