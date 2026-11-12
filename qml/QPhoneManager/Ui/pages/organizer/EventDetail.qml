pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property CalendarArea area

    signal editRequested
    signal deleteRequested(int luid)

    ScrollView {
        id: scroller

        objectName: "eventDetail"
        anchors.fill: parent
        contentWidth: scroller.availableWidth

        ColumnLayout {
            width: scroller.availableWidth
            spacing: Theme.gridUnit * 3

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

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 3

                        Rectangle {
                            Layout.preferredWidth: TypeScale.body
                            Layout.preferredHeight: TypeScale.body
                            radius: TypeScale.body / 2
                            color: root.area.currentColor
                        }

                        Label {
                            objectName: "eventTitle"
                            Layout.fillWidth: true
                            text: root.area.currentSummary.length > 0 ? root.area.currentSummary : qsTr("(No title)")
                            font.pixelSize: TypeScale.title
                            wrapMode: Text.Wrap
                            Accessible.role: Accessible.Heading
                        }
                    }

                    Label {
                        objectName: "eventWhen"
                        Layout.fillWidth: true
                        text: root.area.currentWhen
                        wrapMode: Text.Wrap
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        Button {
                            id: editButton

                            objectName: "editEventButton"
                            text: qsTr("Edit")
                            icon.source: Theme.icon("edit")
                            flat: true
                            enabled: root.area.canEdit && root.area.currentWritable
                            onClicked: root.editRequested()

                            FocusRing {
                                shown: editButton.visualFocus
                            }
                        }

                        Button {
                            id: skipButton

                            objectName: "skipOccurrenceButton"
                            visible: root.area.currentRecurring
                            text: qsTr("Leave out this time")
                            icon.source: Theme.icon("x")
                            flat: true
                            enabled: root.area.canEdit && root.area.currentWritable && !root.area.busy
                            onClicked: root.area.skipCurrentOccurrence()

                            FocusRing {
                                shown: skipButton.visualFocus
                            }
                        }

                        Button {
                            id: deleteButton

                            objectName: "deleteEventButton"
                            text: root.area.currentRecurring ? qsTr("Delete the series") : qsTr("Delete")
                            icon.source: Theme.icon("trash")
                            flat: true
                            enabled: root.area.canEdit && root.area.currentWritable && !root.area.busy
                            onClicked: root.deleteRequested(root.area.currentLuid)

                            FocusRing {
                                shown: deleteButton.visualFocus
                            }
                        }

                        Button {
                            id: backButton

                            objectName: "backToDayButton"
                            text: qsTr("Back to the day")
                            icon.source: Theme.icon("chevron-left")
                            flat: true
                            onClicked: root.area.selectEvent(0, 0)

                            FocusRing {
                                shown: backButton.visualFocus
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gridUnit * 6
                Layout.rightMargin: Theme.gridUnit * 6
                spacing: Theme.gridUnit * 2

                InfoRow {
                    Layout.fillWidth: true
                    visible: root.area.currentLocation.length > 0
                    label: qsTr("Place")
                    value: root.area.currentLocation
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Calendar")
                    value: root.area.currentCalendar
                }

                InfoRow {
                    Layout.fillWidth: true
                    visible: root.area.currentRepeat.length > 0
                    label: qsTr("Repeats")
                    value: root.area.currentRepeat
                }

                InfoRow {
                    Layout.fillWidth: true
                    visible: root.area.currentReminders.length > 0
                    label: qsTr("Reminders")
                    value: root.area.currentReminders
                }
            }

            Label {
                objectName: "eventDescription"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gridUnit * 6
                Layout.rightMargin: Theme.gridUnit * 6
                Layout.bottomMargin: Theme.gridUnit * 4
                visible: root.area.currentDescription.length > 0
                text: root.area.currentDescription
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }
    }
}
