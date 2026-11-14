pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property NotesArea area
    readonly property NoteEditor editor: root.area.editor

    signal finished
    signal deleteRequested(int luid)

    ColumnLayout {
        objectName: "noteForm"
        anchors.fill: parent
        anchors.margins: Theme.gridUnit * 4
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: root.editor.isNew ? qsTr("New note") : qsTr("Edit note")
            font.pixelSize: TypeScale.title
            Accessible.role: Accessible.Heading
        }

        Label {
            Layout.fillWidth: true
            visible: !root.area.canEdit
            text: qsTr("Connect the phone to save changes.")
            color: Theme.warning
            wrapMode: Text.Wrap
        }

        ThemedScrollView {
            id: bodyScroll

            objectName: "noteBodyScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The text area takes the width of the view on its own, so the room
            // for the scroll bar is kept free on the side of the bar.
            leftPadding: bodyScroll.mirrored ? bodyScroll.ScrollBar.vertical.width : 0
            rightPadding: bodyScroll.mirrored ? 0 : bodyScroll.ScrollBar.vertical.width

            ThemedTextArea {
                id: bodyArea

                objectName: "noteBodyArea"
                text: root.editor.body
                placeholderText: qsTr("Write a note")
                wrapMode: TextEdit.Wrap
                Accessible.name: qsTr("Text of the note")
                onTextChanged: {
                    if (bodyArea.text !== root.editor.body)
                        root.editor.body = bodyArea.text;
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

            ThemedTextField {
                id: newCategoryField

                objectName: "noteNewCategoryField"
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

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gridUnit * 3

            Button {
                id: deleteButton

                objectName: "deleteNoteButton"
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

                objectName: "cancelNoteButton"
                text: qsTr("Cancel")
                flat: true
                enabled: !root.editor.busy
                onClicked: root.finished()

                FocusRing {
                    shown: cancelButton.visualFocus
                }
            }

            PrimaryButton {
                id: saveButton

                objectName: "saveNoteButton"
                text: qsTr("Save")
                icon.source: Theme.icon("check")
                enabled: root.editor.canSave
                onClicked: root.editor.save()

                FocusRing {
                    shown: saveButton.visualFocus
                }
            }
        }
    }

    function focusFirstField(): void {
        bodyArea.forceActiveFocus(Qt.OtherFocusReason);
    }
}
