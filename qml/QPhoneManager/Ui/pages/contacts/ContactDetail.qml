pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property ContactsArea area
    readonly property ContactDetailModel detail: root.area.detail

    signal editRequested
    signal deleteRequested(int luid)
    signal exportRequested(int luid)

    EmptyState {
        objectName: "contactDetailEmpty"
        anchors.fill: parent
        visible: !root.detail.hasContact
        text: qsTr("Select a contact in the list.")
    }

    ScrollView {
        id: scroller

        objectName: "contactDetail"
        anchors.fill: parent
        visible: root.detail.hasContact
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
                        spacing: Theme.gridUnit * 4

                        ContactAvatar {
                            photoUrl: root.detail.photoUrl
                            initials: root.detail.initials
                            size: TypeScale.body * 6
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gridUnit

                            Label {
                                objectName: "contactName"
                                Layout.fillWidth: true
                                text: root.detail.displayName
                                font.pixelSize: TypeScale.title
                                wrapMode: Text.Wrap
                                Accessible.role: Accessible.Heading
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: root.detail.work.length > 0
                                text: root.detail.work
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: root.detail.account.length > 0
                                text: qsTr("Account: %1").arg(root.detail.account)
                                font.pixelSize: TypeScale.hint
                                color: Theme.foregroundOnSurfaceVariant
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        Button {
                            id: editButton

                            objectName: "editContactButton"
                            text: qsTr("Edit")
                            icon.source: Theme.icon("edit")
                            enabled: root.area.canEdit
                            onClicked: root.editRequested()

                            FocusRing {
                                shown: editButton.visualFocus
                            }
                        }

                        Button {
                            id: exportOneButton

                            text: qsTr("Export")
                            icon.source: Theme.icon("download")
                            flat: true
                            onClicked: root.exportRequested(root.detail.luid)

                            FocusRing {
                                shown: exportOneButton.visualFocus
                            }
                        }

                        Button {
                            id: deleteButton

                            objectName: "deleteContactButton"
                            text: qsTr("Delete")
                            icon.source: Theme.icon("trash")
                            flat: true
                            enabled: root.area.canEdit
                            onClicked: root.deleteRequested(root.detail.luid)

                            FocusRing {
                                shown: deleteButton.visualFocus
                            }
                        }
                    }
                }
            }

            Repeater {
                model: root.detail

                delegate: ColumnLayout {
                    id: entryRow

                    required property string section
                    required property string label
                    required property string value
                    required property string action
                    required property string actionValue
                    required property bool firstInSection

                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gridUnit * 4
                    Layout.rightMargin: Theme.gridUnit * 4
                    spacing: Theme.gridUnit

                    Label {
                        Layout.topMargin: Theme.gridUnit * 2
                        visible: entryRow.firstInSection
                        text: entryRow.section
                        font.pixelSize: TypeScale.section
                        Accessible.role: Accessible.Heading
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 3

                        Label {
                            Layout.preferredWidth: TypeScale.body * 9
                            Layout.alignment: entryRow.action === "call" ? Qt.AlignVCenter : Qt.AlignTop
                            text: entryRow.label
                            color: Theme.foregroundOnSurfaceVariant
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: entryRow.action === "call" ? Qt.AlignVCenter : Qt.AlignTop
                            text: entryRow.value
                            wrapMode: Text.Wrap
                        }

                        ToolButton {
                            id: callButton

                            visible: entryRow.action === "call"
                            enabled: root.area.canEdit
                            text: qsTr("Call %1").arg(entryRow.actionValue)
                            display: AbstractButton.IconOnly
                            icon.source: Theme.icon("phone")
                            Accessible.name: callButton.text
                            ToolTip.visible: callButton.hovered
                            ToolTip.text: callButton.text
                            onClicked: root.area.call(entryRow.actionValue)

                            FocusRing {
                                shown: callButton.visualFocus
                            }
                        }

                        ToolButton {
                            id: messageButton

                            visible: entryRow.action === "call"
                            text: qsTr("Write a message to %1").arg(entryRow.actionValue)
                            display: AbstractButton.IconOnly
                            icon.source: Theme.icon("message")
                            Accessible.name: messageButton.text
                            ToolTip.visible: messageButton.hovered
                            ToolTip.text: messageButton.text
                            onClicked: root.area.writeMessage(entryRow.actionValue)

                            FocusRing {
                                shown: messageButton.visualFocus
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gridUnit * 4
                Layout.rightMargin: Theme.gridUnit * 4
                Layout.bottomMargin: Theme.gridUnit * 4
                spacing: Theme.gridUnit

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.gridUnit * 2
                    visible: root.detail.groups.length > 0
                    spacing: Theme.gridUnit * 3

                    Label {
                        Layout.preferredWidth: TypeScale.body * 9
                        text: qsTr("Groups")
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.detail.groups
                        wrapMode: Text.Wrap
                    }
                }

                Label {
                    Layout.topMargin: Theme.gridUnit * 2
                    visible: root.detail.note.length > 0
                    text: qsTr("Note")
                    font.pixelSize: TypeScale.section
                    Accessible.role: Accessible.Heading
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.detail.note.length > 0
                    text: root.detail.note
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
