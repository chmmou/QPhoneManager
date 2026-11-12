pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property CalendarArea area
    readonly property int rows: Math.max(1, Math.ceil(root.area.days.count / 7))

    signal dayOpened

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Row {
            Layout.fillWidth: true

            Repeater {
                model: 7

                delegate: Label {
                    required property int index

                    width: dayGrid.cellWidth
                    padding: Theme.gridUnit
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.locale().dayName((Qt.locale().firstDayOfWeek + index) % 7, Locale.ShortFormat)
                    font.pixelSize: TypeScale.hint
                    color: Theme.foregroundOnSurfaceVariant
                }
            }
        }

        GridView {
            id: dayGrid

            objectName: "calendarGrid"
            // The grid is the focus item of its view, also when the view is built anew.
            focus: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            interactive: false
            cellWidth: Math.floor(dayGrid.width / 7)
            cellHeight: Math.floor(dayGrid.height / root.rows)
            model: root.area.days
            currentIndex: root.area.days.rowOf(root.area.currentDate)
            activeFocusOnTab: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.Table
            Accessible.name: qsTr("Days of %1").arg(root.area.periodTitle)

            // A cell is no focus stop of its own; the grid takes the keys and moves the chosen day.
            delegate: Rectangle {
                id: dayCell

                required property int index
                required property date date
                required property int dayNumber
                required property bool inPeriod
                required property bool today
                required property bool selected
                required property string holiday
                required property list<string> entries
                required property list<string> entryColors
                required property int moreCount
                required property string accessibleText

                width: dayGrid.cellWidth
                height: dayGrid.cellHeight
                color: dayCell.selected ? Theme.surfaceVariant : "transparent"
                border.width: 1
                border.color: Theme.surfaceContainer
                Accessible.role: Accessible.ListItem
                Accessible.name: dayCell.accessibleText

                TapHandler {
                    onTapped: {
                        root.area.currentDate = dayCell.date;
                        dayGrid.forceActiveFocus(Qt.MouseFocusReason);
                    }
                    onDoubleTapped: root.dayOpened()
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.gridUnit
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit

                        Rectangle {
                            Layout.preferredWidth: dayNumberLabel.implicitHeight + Theme.gridUnit
                            Layout.preferredHeight: dayNumberLabel.implicitHeight + Theme.gridUnit
                            radius: height / 2
                            color: dayCell.today ? Theme.primary : "transparent"

                            Label {
                                id: dayNumberLabel

                                anchors.centerIn: parent
                                text: dayCell.dayNumber
                                font.bold: dayCell.today
                                color: dayCell.today ? Theme.foregroundOnPrimary : dayCell.inPeriod ? Theme.foregroundOnSurface : Theme.foregroundOnSurfaceVariant
                                opacity: dayCell.inPeriod ? 1 : 0.6
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: dayCell.holiday.length > 0
                            text: dayCell.holiday
                            font.pixelSize: TypeScale.hint
                            color: Theme.error
                            elide: Text.ElideRight
                        }
                    }

                    Repeater {
                        model: dayCell.entries
                        delegate: root.rows === 1 ? weekEntry : monthEntry
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: dayCell.moreCount > 0
                        text: qsTr("%n more", "", dayCell.moreCount)
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                // A week has tall cells, so a title takes up to three lines there.
                Component {
                    id: weekEntry

                    Label {
                        id: weekLabel

                        required property int index
                        required property string modelData
                        readonly property int dotRoom: Theme.gridUnit * 3

                        Layout.fillWidth: true
                        text: weekLabel.modelData
                        font.pixelSize: TypeScale.hint
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight

                        // Only the first line leaves room for the dot, so the lines
                        // below it get the full width of the cell. With this handler
                        // connected, a single line laid out by a layout is not elided,
                        // which is why a month cell does without it.
                        onLineLaidOut: line => {
                            if (line.number === 0) {
                                line.x += weekLabel.dotRoom;
                                line.width -= weekLabel.dotRoom;
                            }
                        }

                        Rectangle {
                            y: Theme.gridUnit
                            width: Theme.gridUnit * 2
                            height: Theme.gridUnit * 2
                            radius: Theme.gridUnit
                            color: dayCell.entryColors[weekLabel.index]
                        }
                    }
                }

                Component {
                    id: monthEntry

                    RowLayout {
                        id: monthRow

                        required property int index
                        required property string modelData

                        Layout.fillWidth: true
                        spacing: Theme.gridUnit

                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: Theme.gridUnit
                            Layout.preferredWidth: Theme.gridUnit * 2
                            Layout.preferredHeight: Theme.gridUnit * 2
                            radius: Theme.gridUnit
                            color: dayCell.entryColors[monthRow.index]
                        }

                        Label {
                            Layout.fillWidth: true
                            text: monthRow.modelData
                            font.pixelSize: TypeScale.hint
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Keys.onLeftPressed: root.area.currentDate = root.shifted(-1)
            Keys.onRightPressed: root.area.currentDate = root.shifted(1)
            Keys.onUpPressed: root.area.currentDate = root.shifted(-7)
            Keys.onDownPressed: root.area.currentDate = root.shifted(7)
            Keys.onReturnPressed: root.dayOpened()
            Keys.onEnterPressed: root.dayOpened()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp) {
                    root.area.previous();
                    event.accepted = true;
                } else if (event.key === Qt.Key_PageDown) {
                    root.area.next();
                    event.accepted = true;
                }
            }
        }
    }

    FocusRing {
        shown: dayGrid.activeFocus
    }

    function shifted(days: int): date {
        const next = new Date(root.area.currentDate);
        next.setDate(next.getDate() + days);
        return next;
    }
}
