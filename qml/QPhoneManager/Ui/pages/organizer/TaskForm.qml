pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property TasksArea area
    readonly property TaskEditor editor: root.area.editor

    signal finished
    signal deleteRequested(int luid)

    ScrollView {
        id: scroller

        objectName: "taskForm"
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
                text: root.editor.isNew ? qsTr("New task") : qsTr("Edit task")
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

                    objectName: "taskSummaryField"
                    Layout.fillWidth: true
                    text: root.editor.summary
                    placeholderText: qsTr("Title")
                    Accessible.name: qsTr("Title")
                    onTextEdited: root.editor.summary = summaryField.text
                }

                CheckBox {
                    id: completedBox

                    objectName: "taskCompletedBox"
                    text: qsTr("Done")
                    checked: root.editor.completed
                    onToggled: root.editor.completed = completedBox.checked

                    FocusRing {
                        shown: completedBox.visualFocus
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
                        id: startField

                        objectName: "taskStartField"
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        text: root.editor.startDate
                        // Shown only while empty: the outlined field would float the form
                        // of the date above the value like a label.
                        placeholderText: startField.text.length > 0 ? "" : root.editor.dateHint
                        Accessible.name: qsTr("Start date")
                        onTextEdited: root.editor.startDate = startField.text
                    }

                    Label {
                        text: qsTr("Due")
                    }

                    TextField {
                        id: dueField

                        objectName: "taskDueField"
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        text: root.editor.dueDate
                        placeholderText: dueField.text.length > 0 ? "" : root.editor.dateHint
                        Accessible.name: qsTr("Due date")
                        Accessible.description: root.editor.dateOrderProblem
                        onTextEdited: root.editor.dueDate = dueField.text
                    }

                    Item {
                        visible: dateOrderLabel.visible
                    }

                    Label {
                        id: dateOrderLabel

                        objectName: "taskDateOrderProblem"
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        visible: root.editor.dateOrderProblem.length > 0
                        text: root.editor.dateOrderProblem
                        color: Theme.error
                        font.pixelSize: TypeScale.hint
                        wrapMode: Text.Wrap
                    }

                    Label {
                        text: qsTr("Reminder")
                    }

                    TextField {
                        id: reminderDateField

                        objectName: "taskReminderDateField"
                        Layout.fillWidth: true
                        text: root.editor.reminderDate
                        placeholderText: reminderDateField.text.length > 0 ? "" : root.editor.dateHint
                        Accessible.name: qsTr("Date of the reminder")
                        onTextEdited: root.editor.reminderDate = reminderDateField.text
                    }

                    TextField {
                        id: reminderTimeField

                        objectName: "taskReminderTimeField"
                        Layout.preferredWidth: TypeScale.body * 6
                        text: root.editor.reminderTime
                        placeholderText: reminderTimeField.text.length > 0 ? "" : root.editor.timeHint
                        Accessible.name: qsTr("Time of the reminder")
                        onTextEdited: root.editor.reminderTime = reminderTimeField.text
                    }

                    Label {
                        text: qsTr("Repeats")
                    }

                    ComboBox {
                        id: repeatBox

                        objectName: "taskRepeatBox"
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        model: root.editor.repeatChoices
                        currentIndex: root.editor.repeatIndex
                        Accessible.name: qsTr("Repeats")
                        onActivated: index => root.editor.repeatIndex = index

                        FocusRing {
                            shown: repeatBox.visualFocus
                        }
                    }

                    Label {
                        text: qsTr("Priority")
                    }

                    ComboBox {
                        id: priorityBox

                        objectName: "taskPriorityBox"
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        model: root.editor.priorityChoices
                        currentIndex: root.editor.priorityIndex
                        Accessible.name: qsTr("Priority")
                        onActivated: index => root.editor.priorityIndex = index

                        FocusRing {
                            shown: priorityBox.visualFocus
                        }
                    }
                }

                Label {
                    text: qsTr("Categories")
                    font.pixelSize: TypeScale.section
                    Accessible.role: Accessible.Heading
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    Repeater {
                        model: root.editor.categoryNames

                        delegate: CheckBox {
                            id: categoryBox

                            required property string modelData

                            text: categoryBox.modelData
                            checked: root.editor.selectedCategories.indexOf(categoryBox.modelData) >= 0
                            onToggled: root.editor.setCategorySelected(categoryBox.modelData, categoryBox.checked)

                            FocusRing {
                                shown: categoryBox.visualFocus
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    TextField {
                        id: newCategoryField

                        objectName: "taskNewCategoryField"
                        Layout.fillWidth: true
                        placeholderText: qsTr("New category")
                        Accessible.name: qsTr("Name of a new category")
                        onAccepted: {
                            if (root.editor.addCategory(newCategoryField.text))
                                newCategoryField.clear();
                        }
                    }

                    Button {
                        id: addCategoryButton

                        text: qsTr("Add category")
                        enabled: newCategoryField.text.trim().length > 0
                        onClicked: {
                            if (root.editor.addCategory(newCategoryField.text))
                                newCategoryField.clear();
                        }

                        FocusRing {
                            shown: addCategoryButton.visualFocus
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

                    objectName: "taskDescriptionArea"
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
                    objectName: "taskProblem"
                    Layout.fillWidth: true
                    // A wrong order of the dates is said under the due date already.
                    visible: root.editor.problem.length > 0 && root.editor.problem !== root.editor.dateOrderProblem
                    text: root.editor.problem
                    color: Theme.error
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.gridUnit * 4
                    spacing: Theme.gridUnit * 3

                    Button {
                        id: deleteButton

                        objectName: "deleteTaskButton"
                        visible: !root.editor.isNew
                        text: qsTr("Delete")
                        icon.source: Theme.icon("trash")
                        flat: true
                        enabled: root.area.canEdit && !root.area.busy
                        onClicked: root.deleteRequested(root.editor.luid)

                        FocusRing {
                            shown: deleteButton.visualFocus
                        }
                    }

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

                        objectName: "cancelTaskButton"
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

                        objectName: "saveTaskButton"
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
