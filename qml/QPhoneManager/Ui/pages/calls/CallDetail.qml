import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property CallsArea area

    signal deleteRequested(int id)

    EmptyState {
        objectName: "callDetailEmpty"
        anchors.fill: parent
        visible: !root.area.hasCurrent
        text: qsTr("Select a call in the list.")
    }

    ThemedScrollView {
        id: scroller

        objectName: "callDetail"
        anchors.fill: parent
        visible: root.area.hasCurrent
        contentWidth: scroller.availableWidth - scroller.ScrollBar.vertical.width

        ColumnLayout {
            width: scroller.contentWidth
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

                        TintedIcon {
                            Layout.preferredWidth: Theme.sidebarIconSize
                            Layout.preferredHeight: Theme.sidebarIconSize
                            name: root.area.currentKind === CallListModel.Missed ? "phone-x" : root.area.currentKind === CallListModel.Incoming ? "phone-incoming" : "phone-outgoing"
                            color: root.area.currentKind === CallListModel.Missed ? Theme.error : Theme.primary
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gridUnit

                            Label {
                                objectName: "callTitle"
                                Layout.fillWidth: true
                                text: root.area.currentTitle
                                font.pixelSize: TypeScale.title
                                wrapMode: Text.Wrap
                                Accessible.role: Accessible.Heading
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.area.currentKindText
                                color: root.area.currentKind === CallListModel.Missed ? Theme.error : Theme.foregroundOnSurfaceVariant
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        Button {
                            id: callButton

                            objectName: "callBackButton"
                            text: qsTr("Call")
                            icon.source: Theme.icon("phone")
                            enabled: root.area.canEdit && root.area.currentCallable
                            onClicked: root.area.call(root.area.currentNumber)

                            FocusRing {
                                shown: callButton.visualFocus
                            }
                        }

                        Button {
                            id: messageButton

                            objectName: "callMessageButton"
                            text: qsTr("Message")
                            icon.source: Theme.icon("message")
                            flat: true
                            enabled: root.area.currentCallable
                            onClicked: root.area.writeMessage(root.area.currentNumber)

                            FocusRing {
                                shown: messageButton.visualFocus
                            }
                        }

                        Button {
                            id: addContactButton

                            objectName: "addCallToContactsButton"
                            text: qsTr("Add to contacts")
                            icon.source: Theme.icon("user")
                            flat: true
                            enabled: root.area.canEdit && root.area.currentCallable && !root.area.currentInContacts
                            onClicked: root.area.addCurrentToContacts()

                            FocusRing {
                                shown: addContactButton.visualFocus
                            }
                        }

                        Button {
                            id: deleteButton

                            objectName: "deleteCallButton"
                            text: qsTr("Delete")
                            icon.source: Theme.icon("trash")
                            flat: true
                            enabled: root.area.canEdit
                            onClicked: root.deleteRequested(root.area.currentId)

                            FocusRing {
                                shown: deleteButton.visualFocus
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
                spacing: Theme.gridUnit * 2

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Number")
                    value: root.area.currentNumber.length > 0 ? root.area.currentNumber : qsTr("Hidden")
                    valueObjectName: "callNumber"
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Time")
                    value: root.area.currentTimeText
                }

                InfoRow {
                    Layout.fillWidth: true
                    visible: root.area.currentDurationText.length > 0
                    label: qsTr("Duration")
                    value: root.area.currentDurationText
                }

                InfoRow {
                    Layout.fillWidth: true
                    visible: root.area.currentSimText.length > 0
                    label: qsTr("SIM card")
                    value: root.area.currentSimText
                }
            }
        }
    }
}
