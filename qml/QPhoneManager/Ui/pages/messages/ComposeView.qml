pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property MessagesArea area
    readonly property MessageComposer composer: root.area.composer

    signal cancelled
    signal deleteRequested(int luid)

    objectName: "composeView"

    ScrollView {
        id: scroller

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

                    Label {
                        objectName: "composeTitle"
                        Layout.fillWidth: true
                        text: root.composer.editingLuid > 0 ? qsTr("Change the scheduled message") : qsTr("New message")
                        font.pixelSize: TypeScale.title
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.Heading
                    }

                    Label {
                        text: qsTr("To")
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                    }

                    Flow {
                        Layout.fillWidth: true
                        visible: root.composer.recipients.length > 0
                        spacing: Theme.gridUnit * 2

                        Repeater {
                            model: root.composer.recipients

                            delegate: Button {
                                id: recipientChip

                                required property int index
                                required property string modelData

                                objectName: "recipientChip" + recipientChip.index
                                text: recipientChip.modelData
                                icon.source: Theme.icon("x")
                                flat: true
                                Accessible.name: qsTr("Remove %1").arg(recipientChip.modelData)
                                onClicked: root.composer.removeRecipient(recipientChip.index)

                                FocusRing {
                                    shown: recipientChip.visualFocus
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        TextField {
                            id: numberField

                            objectName: "recipientField"
                            Layout.fillWidth: true
                            text: root.composer.recipientText
                            placeholderText: qsTr("Enter a number")
                            inputMethodHints: Qt.ImhDialableCharactersOnly
                            Accessible.name: qsTr("Add a recipient")
                            onTextEdited: root.composer.recipientText = numberField.text
                            onAccepted: root.composer.addRecipient()
                        }

                        Button {
                            id: addButton

                            objectName: "addRecipientButton"
                            text: qsTr("Add")
                            icon.source: Theme.icon("plus")
                            enabled: root.composer.recipientText.trim().length > 0
                            onClicked: root.composer.addRecipient()

                            FocusRing {
                                shown: addButton.visualFocus
                            }
                        }
                    }

                    // The outlined fields show their focus themselves; a ring on top
                    // would cross the floating label.
                    TextArea {
                        id: textArea

                        objectName: "messageTextArea"
                        Layout.fillWidth: true
                        Layout.preferredHeight: TypeScale.body * 8
                        text: root.composer.text
                        placeholderText: qsTr("Write a message")
                        wrapMode: TextEdit.Wrap
                        Accessible.name: qsTr("Message text")
                        onTextChanged: {
                            if (textArea.text !== root.composer.text)
                                root.composer.text = textArea.text;
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 3

                        Label {
                            objectName: "remainingText"
                            text: root.composer.remainingText
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                        }

                        Label {
                            objectName: "partsText"
                            visible: root.composer.partsText.length > 0
                            text: root.composer.partsText
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.composer.unicode
                        text: qsTr("The text holds special characters, so fewer characters fit into one message.")
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.composer.simChoices.length > 2
                        spacing: Theme.gridUnit * 2

                        Label {
                            text: qsTr("SIM card")
                        }

                        ComboBox {
                            id: simBox

                            objectName: "simBox"
                            Layout.fillWidth: true
                            model: root.composer.simChoices
                            currentIndex: root.composer.simIndex
                            Accessible.name: qsTr("SIM card")
                            onActivated: index => root.composer.simIndex = index

                            FocusRing {
                                shown: simBox.visualFocus
                            }
                        }
                    }

                    CheckBox {
                        id: laterBox

                        objectName: "scheduleBox"
                        text: qsTr("Send later")
                        checked: root.composer.scheduled
                        enabled: root.composer.editingLuid === 0
                        onToggled: root.composer.scheduled = laterBox.checked

                        FocusRing {
                            shown: laterBox.visualFocus
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        visible: root.composer.scheduled
                        columns: 2
                        columnSpacing: Theme.gridUnit * 2
                        rowSpacing: Theme.gridUnit * 2

                        Label {
                            text: qsTr("Date")
                        }

                        TextField {
                            id: dateField

                            objectName: "scheduleDateField"
                            Layout.fillWidth: true
                            text: root.composer.scheduleDate
                            Accessible.name: qsTr("Date to send")
                            onTextEdited: root.composer.scheduleDate = dateField.text
                        }

                        Label {
                            text: qsTr("Time")
                        }

                        TextField {
                            id: timeField

                            objectName: "scheduleTimeField"
                            Layout.fillWidth: true
                            text: root.composer.scheduleTime
                            Accessible.name: qsTr("Time to send")
                            onTextEdited: root.composer.scheduleTime = timeField.text
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.composer.scheduleProblem.length > 0
                        text: root.composer.scheduleProblem
                        color: Theme.error
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.composer.scheduled
                        text: qsTr("The phone sends the message on its own at this time, also when this computer is off.")
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.area.canEdit
                        text: qsTr("Connect the phone to send messages.")
                        color: Theme.warning
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        Button {
                            id: deleteButton

                            objectName: "deleteScheduledButton"
                            visible: root.composer.editingLuid > 0
                            text: qsTr("Delete")
                            icon.source: Theme.icon("trash")
                            flat: true
                            enabled: root.area.canEdit && !root.composer.busy
                            onClicked: root.deleteRequested(root.composer.editingLuid)

                            FocusRing {
                                shown: deleteButton.visualFocus
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            id: cancelButton

                            objectName: "cancelComposeButton"
                            text: qsTr("Cancel")
                            flat: true
                            onClicked: root.cancelled()

                            FocusRing {
                                shown: cancelButton.visualFocus
                            }
                        }

                        Button {
                            id: submitButton

                            objectName: "submitMessageButton"
                            text: root.composer.scheduled ? (root.composer.editingLuid > 0 ? qsTr("Save") : qsTr("Schedule")) : qsTr("Send")
                            icon.source: Theme.icon(root.composer.scheduled ? "clock" : "send")
                            highlighted: true
                            enabled: root.composer.canSubmit
                            onClicked: root.submit()

                            FocusRing {
                                shown: submitButton.visualFocus
                            }
                        }
                    }
                }
            }
        }
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: root.visible && root.composer.canSubmit
        onActivated: root.submit()
    }

    Component.onCompleted: {
        if (root.composer.recipients.length === 0)
            numberField.forceActiveFocus(Qt.OtherFocusReason);
        else
            textArea.forceActiveFocus(Qt.OtherFocusReason);
    }

    function submit(): void {
        if (root.composer.scheduled)
            root.composer.schedule();
        else
            root.composer.send();
    }
}
