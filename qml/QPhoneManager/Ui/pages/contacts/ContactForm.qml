pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property ContactsArea area
    readonly property ContactEditor editor: root.area.editor

    signal finished
    signal photoRequested

    ThemedScrollView {
        id: scroller

        objectName: "contactForm"
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
                text: root.editor.isNew ? qsTr("New contact") : qsTr("Edit contact")
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
                spacing: Theme.gridUnit * 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 4

                    ContactAvatar {
                        photoUrl: root.editor.photoUrl
                        initials: root.editor.givenName.slice(0, 1).toUpperCase() + root.editor.familyName.slice(0, 1).toUpperCase()
                        size: TypeScale.body * 6
                    }

                    ColumnLayout {
                        spacing: Theme.gridUnit * 2

                        Button {
                            id: photoButton

                            objectName: "choosePhotoButton"
                            text: root.editor.hasPhoto ? qsTr("Change photo") : qsTr("Choose photo")
                            icon.source: Theme.icon("photo")
                            onClicked: root.photoRequested()

                            FocusRing {
                                shown: photoButton.visualFocus
                            }
                        }

                        Button {
                            id: removePhotoButton

                            text: qsTr("Remove photo")
                            flat: true
                            enabled: root.editor.hasPhoto
                            onClicked: root.editor.clearPhoto()

                            FocusRing {
                                shown: removePhotoButton.visualFocus
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.editor.isNew && root.editor.accountNames.length > 0
                    spacing: Theme.gridUnit * 3

                    Label {
                        text: qsTr("Save in account")
                    }

                    ThemedComboBox {
                        id: accountBox

                        objectName: "accountBox"
                        Layout.fillWidth: true
                        model: root.editor.accountNames
                        currentIndex: root.editor.accountIndex
                        Accessible.name: qsTr("Save in account")
                        onActivated: index => root.editor.accountIndex = index
                        Component.onCompleted: Theme.calmPopup(accountBox.popup)

                        FocusRing {
                            shown: accountBox.visualFocus
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: !root.editor.isNew && root.editor.accountText.length > 0
                    text: qsTr("Account: %1").arg(root.editor.accountText)
                    color: Theme.foregroundOnSurfaceVariant
                    wrapMode: Text.Wrap
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.gridUnit * 3
                    rowSpacing: Theme.gridUnit * 2

                    ThemedTextField {
                        id: givenField

                        objectName: "givenNameField"
                        Layout.fillWidth: true
                        text: root.editor.givenName
                        placeholderText: qsTr("First name")
                        Accessible.name: qsTr("First name")
                        onTextEdited: root.editor.givenName = givenField.text
                    }

                    ThemedTextField {
                        id: familyField

                        objectName: "familyNameField"
                        Layout.fillWidth: true
                        text: root.editor.familyName
                        placeholderText: qsTr("Last name")
                        Accessible.name: qsTr("Last name")
                        onTextEdited: root.editor.familyName = familyField.text
                    }

                    ThemedTextField {
                        id: middleField

                        Layout.fillWidth: true
                        text: root.editor.middleName
                        placeholderText: qsTr("Middle name")
                        Accessible.name: qsTr("Middle name")
                        onTextEdited: root.editor.middleName = middleField.text
                    }

                    ThemedTextField {
                        id: nicknameField

                        Layout.fillWidth: true
                        text: root.editor.nickname
                        placeholderText: qsTr("Nickname")
                        Accessible.name: qsTr("Nickname")
                        onTextEdited: root.editor.nickname = nicknameField.text
                    }

                    ThemedTextField {
                        id: prefixField

                        Layout.fillWidth: true
                        text: root.editor.prefix
                        placeholderText: qsTr("Title before the name, such as Dr.")
                        Accessible.name: qsTr("Title before the name")
                        onTextEdited: root.editor.prefix = prefixField.text
                    }

                    ThemedTextField {
                        id: suffixField

                        Layout.fillWidth: true
                        text: root.editor.suffix
                        placeholderText: qsTr("Addition after the name, such as jr.")
                        Accessible.name: qsTr("Addition after the name")
                        onTextEdited: root.editor.suffix = suffixField.text
                    }

                    ThemedTextField {
                        id: organizationField

                        Layout.fillWidth: true
                        text: root.editor.organization
                        placeholderText: qsTr("Company")
                        Accessible.name: qsTr("Company")
                        onTextEdited: root.editor.organization = organizationField.text
                    }

                    ThemedTextField {
                        id: departmentField

                        Layout.fillWidth: true
                        text: root.editor.department
                        placeholderText: qsTr("Department")
                        Accessible.name: qsTr("Department")
                        onTextEdited: root.editor.department = departmentField.text
                    }

                    ThemedTextField {
                        id: jobField

                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        text: root.editor.jobTitle
                        placeholderText: qsTr("Job title")
                        Accessible.name: qsTr("Job title")
                        onTextEdited: root.editor.jobTitle = jobField.text
                    }
                }

                ContactValueSection {
                    Layout.fillWidth: true
                    entries: root.editor.phones
                    title: qsTr("Phone numbers")
                    addText: qsTr("Add number")
                    valueName: qsTr("phone number")
                    placeholder: qsTr("Number")
                }

                ContactValueSection {
                    Layout.fillWidth: true
                    entries: root.editor.emails
                    title: qsTr("Mail addresses")
                    addText: qsTr("Add mail address")
                    valueName: qsTr("mail address")
                    placeholder: qsTr("Mail address")
                }

                ContactValueSection {
                    Layout.fillWidth: true
                    entries: root.editor.addresses
                    title: qsTr("Addresses")
                    addText: qsTr("Add address")
                    valueName: qsTr("address")
                    isAddress: true
                }

                ContactValueSection {
                    Layout.fillWidth: true
                    entries: root.editor.websites
                    title: qsTr("Websites")
                    addText: qsTr("Add website")
                    valueName: qsTr("website")
                    placeholder: qsTr("Address of the website")
                }

                ContactValueSection {
                    Layout.fillWidth: true
                    entries: root.editor.ims
                    title: qsTr("Messengers")
                    addText: qsTr("Add messenger")
                    valueName: qsTr("messenger name")
                    placeholder: qsTr("Name in the messenger")
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    Label {
                        text: qsTr("Dates")
                        font.pixelSize: TypeScale.section
                        Accessible.role: Accessible.Heading
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 3

                        ThemedTextField {
                            id: birthdayField

                            objectName: "birthdayField"
                            Layout.fillWidth: true
                            text: root.editor.birthday
                            placeholderText: qsTr("Birthday, for example %1").arg(root.editor.dateExample)
                            Accessible.name: qsTr("Birthday")
                            onTextEdited: root.editor.birthday = birthdayField.text
                        }

                        ThemedTextField {
                            id: anniversaryField

                            Layout.fillWidth: true
                            text: root.editor.anniversary
                            placeholderText: qsTr("Anniversary, for example %1").arg(root.editor.dateExample)
                            Accessible.name: qsTr("Anniversary")
                            onTextEdited: root.editor.anniversary = anniversaryField.text
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.editor.datesValid
                        text: qsTr("Enter dates like %1.").arg(root.editor.dateExample)
                        color: Theme.error
                        wrapMode: Text.Wrap
                    }
                }

                ContactValueSection {
                    Layout.fillWidth: true
                    entries: root.editor.events
                    title: qsTr("Other dates")
                    addText: qsTr("Add date")
                    valueName: qsTr("date")
                    placeholder: qsTr("Date such as 2024-05-31")
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    Label {
                        text: qsTr("Groups")
                        font.pixelSize: TypeScale.section
                        Accessible.role: Accessible.Heading
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        Repeater {
                            model: root.editor.groupNames

                            delegate: CheckBox {
                                id: groupBox

                                required property string modelData

                                text: groupBox.modelData
                                checked: root.editor.selectedGroups.indexOf(groupBox.modelData) >= 0
                                onToggled: root.editor.setGroupSelected(groupBox.modelData, groupBox.checked)

                                FocusRing {
                                    shown: groupBox.visualFocus
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        ThemedTextField {
                            id: newGroupField

                            Layout.fillWidth: true
                            placeholderText: qsTr("New group")
                            Accessible.name: qsTr("Name of a new group")
                            onAccepted: {
                                if (root.editor.addGroup(newGroupField.text))
                                    newGroupField.clear();
                            }
                        }

                        Button {
                            id: addGroupButton

                            text: qsTr("Add group")
                            enabled: newGroupField.text.trim().length > 0
                            onClicked: {
                                if (root.editor.addGroup(newGroupField.text))
                                    newGroupField.clear();
                            }

                            FocusRing {
                                shown: addGroupButton.visualFocus
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    Label {
                        text: qsTr("Note")
                        font.pixelSize: TypeScale.section
                        Accessible.role: Accessible.Heading
                    }

                    ThemedTextArea {
                        id: noteArea

                        Layout.fillWidth: true
                        Layout.preferredHeight: TypeScale.body * 6
                        text: root.editor.note
                        wrapMode: TextEdit.Wrap
                        Accessible.name: qsTr("Note")
                        onTextChanged: {
                            if (noteArea.activeFocus)
                                root.editor.note = noteArea.text;
                        }
                    }
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

                        objectName: "cancelContactButton"
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

                        objectName: "saveContactButton"
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
        }
    }

    function focusFirstField(): void {
        givenField.forceActiveFocus(Qt.OtherFocusReason);
    }
}
