pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property CalendarArea area
    required property string emptyText
    // Headings per day, for a list over many days.
    property bool showDays: false

    signal openRequested
    signal deleteRequested(int luid)

    ListView {
        id: list

        objectName: "eventList"
        anchors.fill: parent
        visible: list.count > 0
        // The list scrolls through more appointments than fit.
        clip: true
        model: root.area.list
        // Starts at the appearance shown, so that a list made anew keeps it.
        currentIndex: root.area.currentRow
        activeFocusOnTab: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        section.property: root.showDays ? "dayText" : ""
        section.criteria: ViewSection.FullString
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Appointments")
        ScrollBar.vertical: ScrollBar {}

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
        // focus stop, and the row shows the current entry by its colour.
        delegate: Rectangle {
            id: eventRow

            required property int index
            required property int luid
            required property string summary
            required property string location
            required property string timeText
            required property color entryColor
            required property bool holiday
            required property bool recurring

            width: ListView.view.width
            implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 4
            color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: eventRow.summary.length > 0 ? eventRow.summary : qsTr("(No title)")
            Accessible.description: eventRow.timeText

            TapHandler {
                onTapped: {
                    list.currentIndex = eventRow.index;
                    list.forceActiveFocus(Qt.MouseFocusReason);
                    root.open(eventRow.index);
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

                Rectangle {
                    Layout.preferredWidth: Theme.gridUnit
                    Layout.fillHeight: true
                    radius: Theme.gridUnit / 2
                    color: eventRow.entryColor
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: eventRow.summary.length > 0 ? eventRow.summary : qsTr("(No title)")
                        font.italic: eventRow.holiday
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: eventRow.location.length > 0 ? qsTr("%1, %2").arg(eventRow.timeText).arg(eventRow.location) : eventRow.timeText
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }
                }

                TintedIcon {
                    Layout.preferredWidth: Theme.listIconSize
                    Layout.preferredHeight: Theme.listIconSize
                    visible: eventRow.recurring
                    name: "refresh"
                    color: Theme.foregroundOnSurfaceVariant
                }
            }
        }

        Keys.onReturnPressed: root.open(list.currentIndex)
        Keys.onEnterPressed: root.open(list.currentIndex)
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
        objectName: "eventListEmpty"
        anchors.fill: parent
        visible: list.count === 0
        text: root.emptyText
    }

    Connections {
        target: root.area

        function onCurrentChanged(): void {
            list.currentIndex = root.area.currentRow;
        }
    }

    Connections {
        target: root.area.list

        function onCountChanged(): void {
            list.currentIndex = root.area.currentRow;
        }

        function onModelReset(): void {
            list.currentIndex = root.area.currentRow;
        }
    }

    function luidAt(row: int): int {
        return root.area.list.luidAt(row);
    }

    function open(row: int): void {
        if (row < 0)
            return;
        root.area.selectRow(row);
        root.openRequested();
    }
}
