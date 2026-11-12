pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property CalendarArea area
    readonly property EventEditor editor: root.area.editor

    signal finished

    ScrollView {
        id: scroller

        objectName: "eventForm"
        anchors.fill: parent
        contentWidth: scroller.availableWidth

        ColumnLayout {
            width: scroller.availableWidth
            spacing: Theme.gridUnit * 4

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Theme.gridUnit * 4
                Layout.leftMargin: Theme.gridUnit * 4
                Layout.rightMargin: Theme.gridUnit * 4
                text: root.editor.isNew ? qsTr("New appointment") : qsTr("Edit appointment")
                font.pixelSize: TypeScale.title
                Accessible.role: Accessible.Heading
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gridUnit * 4
                Layout.rightMargin: Theme.gridUnit * 4
                visible: !root.area.canEdit
                text: qsTr("Connect the phone to save changes.")
                color: Theme.warning
                wrapMode: Text.Wrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gridUnit * 4
                Layout.rightMargin: Theme.gridUnit * 4
                spacing: Theme.gridUnit * 3

                TextField {
                    id: summaryField

                    objectName: "eventSummaryField"
                    Layout.fillWidth: true
                    text: root.editor.summary
                    placeholderText: qsTr("Title")
                    Accessible.name: qsTr("Title")
                    onTextEdited: root.editor.summary = summaryField.text
                }

                TextField {
                    id: locationField

                    objectName: "eventLocationField"
                    Layout.fillWidth: true
                    text: root.editor.location
                    placeholderText: qsTr("Place")
                    Accessible.name: qsTr("Place")
                    onTextEdited: root.editor.location = locationField.text
                }

                CheckBox {
                    id: allDayBox

                    objectName: "allDayBox"
                    text: qsTr("All day")
                    checked: root.editor.allDay
                    onToggled: root.editor.allDay = allDayBox.checked

                    FocusRing {
                        shown: allDayBox.visualFocus
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: Theme.gridUnit * 3
                    rowSpacing: Theme.gridUnit * 2

                    Label {
                        text: qsTr("Start")
                    }

                    TextField {
                        id: startDateField

                        objectName: "eventStartDateField"
                        Layout.fillWidth: true
                        text: root.editor.startDate
                        // Shown only while empty: the outlined field would float the form
                        // of the date above the value like a label.
                        placeholderText: startDateField.text.length > 0 ? "" : root.editor.dateHint
                        Accessible.name: qsTr("Start date")
                        onTextEdited: root.editor.startDate = startDateField.text
                    }

                    TextField {
                        id: startTimeField

                        objectName: "eventStartTimeField"
                        Layout.preferredWidth: TypeScale.body * 6
                        enabled: !root.editor.allDay
                        text: root.editor.startTime
                        placeholderText: startTimeField.text.length > 0 ? "" : root.editor.timeHint
                        Accessible.name: qsTr("Start time")
                        onTextEdited: root.editor.startTime = startTimeField.text
                    }

                    Label {
                        text: qsTr("End")
                    }

                    TextField {
                        id: endDateField

                        objectName: "eventEndDateField"
                        Layout.fillWidth: true
                        text: root.editor.endDate
                        placeholderText: endDateField.text.length > 0 ? "" : root.editor.dateHint
                        Accessible.name: qsTr("End date")
                        onTextEdited: root.editor.endDate = endDateField.text
                    }

                    TextField {
                        id: endTimeField

                        objectName: "eventEndTimeField"
                        Layout.preferredWidth: TypeScale.body * 6
                        enabled: !root.editor.allDay
                        text: root.editor.endTime
                        placeholderText: endTimeField.text.length > 0 ? "" : root.editor.timeHint
                        Accessible.name: qsTr("End time")
                        onTextEdited: root.editor.endTime = endTimeField.text
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.gridUnit * 3
                    rowSpacing: Theme.gridUnit * 2

                    Label {
                        visible: root.editor.isNew
                        text: qsTr("Calendar")
                    }

                    ComboBox {
                        id: calendarBox

                        objectName: "eventCalendarBox"
                        Layout.fillWidth: true
                        visible: root.editor.isNew
                        model: root.editor.calendarNames
                        currentIndex: root.editor.calendarIndex
                        Accessible.name: qsTr("Calendar")
                        onActivated: index => root.editor.calendarIndex = index

                        FocusRing {
                            shown: calendarBox.visualFocus
                        }
                    }

                    Label {
                        text: qsTr("Repeats")
                    }

                    ComboBox {
                        id: repeatBox

                        objectName: "eventRepeatBox"
                        Layout.fillWidth: true
                        model: root.editor.repeatChoices
                        currentIndex: root.editor.repeatIndex
                        Accessible.name: qsTr("Repeats")
                        onActivated: index => root.editor.repeatIndex = index

                        FocusRing {
                            shown: repeatBox.visualFocus
                        }
                    }

                    Label {
                        text: qsTr("Colour")
                    }

                    ComboBox {
                        id: colorBox

                        objectName: "eventColorBox"
                        Layout.fillWidth: true
                        model: root.editor.colorChoices
                        currentIndex: root.editor.colorIndex
                        Accessible.name: qsTr("Colour")
                        onActivated: index => root.editor.colorIndex = index

                        FocusRing {
                            shown: colorBox.visualFocus
                        }
                    }

                    Label {
                        text: qsTr("Shown as")
                    }

                    ComboBox {
                        id: availabilityBox

                        objectName: "eventAvailabilityBox"
                        Layout.fillWidth: true
                        model: root.editor.availabilityChoices
                        currentIndex: root.editor.availabilityIndex
                        Accessible.name: qsTr("Shown as")
                        onActivated: index => root.editor.availabilityIndex = index

                        FocusRing {
                            shown: availabilityBox.visualFocus
                        }
                    }

                    Label {
                        text: qsTr("Visible to")
                    }

                    ComboBox {
                        id: visibilityBox

                        objectName: "eventVisibilityBox"
                        Layout.fillWidth: true
                        model: root.editor.visibilityChoices
                        currentIndex: root.editor.visibilityIndex
                        Accessible.name: qsTr("Visible to")
                        onActivated: index => root.editor.visibilityIndex = index

                        FocusRing {
                            shown: visibilityBox.visualFocus
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.editor.exceptionCount > 0
                    spacing: Theme.gridUnit * 2

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("%n time(s) left out of the series.", "", root.editor.exceptionCount)
                        wrapMode: Text.Wrap
                    }

                    Button {
                        id: exceptionsButton

                        text: qsTr("Bring back")
                        flat: true
                        onClicked: root.editor.clearExceptions()

                        FocusRing {
                            shown: exceptionsButton.visualFocus
                        }
                    }
                }

                Label {
                    text: qsTr("Reminders")
                    font.pixelSize: TypeScale.section
                    Accessible.role: Accessible.Heading
                }

                Repeater {
                    model: root.editor.reminders

                    delegate: RowLayout {
                        id: reminderRow

                        required property int index
                        required property string modelData

                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        Label {
                            Layout.fillWidth: true
                            text: reminderRow.modelData
                        }

                        ToolButton {
                            id: removeReminderButton

                            text: qsTr("Remove the reminder %1").arg(reminderRow.modelData)
                            display: AbstractButton.IconOnly
                            icon.source: Theme.icon("x")
                            Accessible.name: removeReminderButton.text
                            ToolTip.visible: removeReminderButton.hovered
                            ToolTip.text: removeReminderButton.text
                            onClicked: root.editor.removeReminder(reminderRow.index)

                            FocusRing {
                                shown: removeReminderButton.visualFocus
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    ComboBox {
                        id: reminderBox

                        objectName: "reminderChoiceBox"
                        Layout.fillWidth: true
                        model: root.editor.reminderChoices
                        currentIndex: 3
                        Accessible.name: qsTr("Time of a new reminder")

                        FocusRing {
                            shown: reminderBox.visualFocus
                        }
                    }

                    Button {
                        id: addReminderButton

                        objectName: "addReminderButton"
                        text: qsTr("Add reminder")
                        icon.source: Theme.icon("plus")
                        onClicked: root.editor.addReminder(reminderBox.currentIndex)

                        FocusRing {
                            shown: addReminderButton.visualFocus
                        }
                    }
                }

                Label {
                    text: qsTr("Description")
                    font.pixelSize: TypeScale.section
                    Accessible.role: Accessible.Heading
                }

                TextArea {
                    id: descriptionArea

                    objectName: "eventDescriptionArea"
                    Layout.fillWidth: true
                    Layout.preferredHeight: TypeScale.body * 6
                    text: root.editor.description
                    wrapMode: TextEdit.Wrap
                    Accessible.name: qsTr("Description")
                    onTextChanged: {
                        if (descriptionArea.text !== root.editor.description)
                            root.editor.description = descriptionArea.text;
                    }
                }

                Label {
                    objectName: "eventProblem"
                    Layout.fillWidth: true
                    visible: root.editor.problem.length > 0
                    text: root.editor.problem
                    color: Theme.error
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.gridUnit * 4
                    spacing: Theme.gridUnit * 3

                    Item {
                        Layout.fillWidth: true
                    }

                    BusyIndicator {
                        Layout.preferredWidth: TypeScale.body * 2
                        Layout.preferredHeight: TypeScale.body * 2
                        visible: root.editor.busy
                        running: root.editor.busy
                        Accessible.name: qsTr("Saving")
                    }

                    Button {
                        id: cancelButton

                        objectName: "cancelEventButton"
                        text: qsTr("Cancel")
                        flat: true
                        enabled: !root.editor.busy
                        onClicked: root.finished()

                        FocusRing {
                            shown: cancelButton.visualFocus
                        }
                    }

                    Button {
                        id: saveButton

                        objectName: "saveEventButton"
                        text: qsTr("Save")
                        icon.source: Theme.icon("check")
                        highlighted: true
                        enabled: root.editor.canSave
                        onClicked: root.editor.save()

                        FocusRing {
                            shown: saveButton.visualFocus
                        }
                    }
                }
            }
        }
    }

    function focusFirstField(): void {
        summaryField.forceActiveFocus(Qt.OtherFocusReason);
    }
}
