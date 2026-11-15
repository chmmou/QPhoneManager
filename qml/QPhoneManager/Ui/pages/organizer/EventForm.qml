pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property CalendarArea area
    readonly property EventEditor editor: root.area.editor

    signal finished

    ThemedScrollView {
        id: scroller

        objectName: "eventForm"
        anchors.fill: parent
        contentWidth: scroller.availableWidth - scroller.ScrollBar.vertical.width

        ColumnLayout {
            width: scroller.contentWidth
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

                ThemedTextField {
                    id: summaryField

                    objectName: "eventSummaryField"
                    Layout.fillWidth: true
                    text: root.editor.summary
                    placeholderText: qsTr("Title")
                    Accessible.name: qsTr("Title")
                    onTextEdited: root.editor.summary = summaryField.text
                }

                ThemedTextField {
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
                        shown: allDayBox.activeFocus
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

                    ThemedTextField {
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

                    ThemedTextField {
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

                    ThemedTextField {
                        id: endDateField

                        objectName: "eventEndDateField"
                        Layout.fillWidth: true
                        text: root.editor.endDate
                        placeholderText: endDateField.text.length > 0 ? "" : root.editor.dateHint
                        Accessible.name: qsTr("End date")
                        onTextEdited: root.editor.endDate = endDateField.text
                    }

                    ThemedTextField {
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

                    ThemedComboBox {
                        id: calendarBox

                        objectName: "eventCalendarBox"
                        Layout.fillWidth: true
                        visible: root.editor.isNew
                        model: root.editor.calendarNames
                        currentIndex: root.editor.calendarIndex
                        Accessible.name: qsTr("Calendar")
                        onActivated: index => root.editor.calendarIndex = index
                        Component.onCompleted: Theme.calmPopup(calendarBox.popup)

                        FocusRing {
                            shown: calendarBox.activeFocus
                        }
                    }

                    Label {
                        text: qsTr("Repeats")
                    }

                    ThemedComboBox {
                        id: repeatBox

                        objectName: "eventRepeatBox"
                        Layout.fillWidth: true
                        model: root.editor.repeatChoices
                        currentIndex: root.editor.repeatIndex
                        Accessible.name: qsTr("Repeats")
                        onActivated: index => root.editor.repeatIndex = index
                        Component.onCompleted: Theme.calmPopup(repeatBox.popup)

                        FocusRing {
                            shown: repeatBox.activeFocus
                        }
                    }

                    Label {
                        text: qsTr("Colour")
                    }

                    ThemedComboBox {
                        id: colorBox

                        objectName: "eventColorBox"
                        Layout.fillWidth: true
                        model: root.editor.colorChoices
                        currentIndex: root.editor.colorIndex
                        Accessible.name: qsTr("Colour")
                        onActivated: index => root.editor.colorIndex = index
                        Component.onCompleted: Theme.calmPopup(colorBox.popup)

                        FocusRing {
                            shown: colorBox.activeFocus
                        }
                    }

                    Label {
                        text: qsTr("Shown as")
                    }

                    ThemedComboBox {
                        id: availabilityBox

                        objectName: "eventAvailabilityBox"
                        Layout.fillWidth: true
                        model: root.editor.availabilityChoices
                        currentIndex: root.editor.availabilityIndex
                        Accessible.name: qsTr("Shown as")
                        onActivated: index => root.editor.availabilityIndex = index
                        Component.onCompleted: Theme.calmPopup(availabilityBox.popup)

                        FocusRing {
                            shown: availabilityBox.activeFocus
                        }
                    }

                    Label {
                        text: qsTr("Visible to")
                    }

                    ThemedComboBox {
                        id: visibilityBox

                        objectName: "eventVisibilityBox"
                        Layout.fillWidth: true
                        model: root.editor.visibilityChoices
                        currentIndex: root.editor.visibilityIndex
                        Accessible.name: qsTr("Visible to")
                        onActivated: index => root.editor.visibilityIndex = index
                        Component.onCompleted: Theme.calmPopup(visibilityBox.popup)

                        FocusRing {
                            shown: visibilityBox.activeFocus
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
                            shown: exceptionsButton.activeFocus
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
                                shown: removeReminderButton.activeFocus
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    ThemedComboBox {
                        id: reminderBox

                        objectName: "reminderChoiceBox"
                        Layout.fillWidth: true
                        model: root.editor.reminderChoices
                        currentIndex: 3
                        Accessible.name: qsTr("Time of a new reminder")
                        Component.onCompleted: Theme.calmPopup(reminderBox.popup)

                        FocusRing {
                            shown: reminderBox.activeFocus
                        }
                    }

                    Button {
                        id: addReminderButton

                        objectName: "addReminderButton"
                        text: qsTr("Add reminder")
                        icon.source: Theme.icon("plus")
                        onClicked: root.editor.addReminder(reminderBox.currentIndex)

                        FocusRing {
                            shown: addReminderButton.activeFocus
                        }
                    }
                }

                Label {
                    text: qsTr("Description")
                    font.pixelSize: TypeScale.section
                    Accessible.role: Accessible.Heading
                }

                ThemedTextArea {
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
                            shown: cancelButton.activeFocus
                        }
                    }

                    PrimaryButton {
                        id: saveButton

                        objectName: "saveEventButton"
                        text: qsTr("Save")
                        icon.source: Theme.icon("check")
                        enabled: root.editor.canSave
                        onClicked: root.editor.save()

                        FocusRing {
                            shown: saveButton.activeFocus
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
