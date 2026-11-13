import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    property string explanation: ""
    property string ownText: ""
    property string phoneText: ""

    signal keepOwnRequested
    signal takePhoneRequested

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 44, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 44)
    height: Math.min(TypeScale.body * 34, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 34)
    modal: true
    Overlay.modal: Scrim {}
    closePolicy: Popup.CloseOnEscape
    Material.elevation: 0

    footer: DialogButtonBox {
        Button {
            id: keepOwnButton

            objectName: "keepOwnButton"
            text: qsTr("Save my version")
            onClicked: {
                root.close();
                root.keepOwnRequested();
            }

            FocusRing {
                shown: keepOwnButton.visualFocus
            }
        }

        Button {
            id: takePhoneButton

            objectName: "takePhoneButton"
            text: qsTr("Keep the phone's version")
            onClicked: {
                root.close();
                root.takePhoneRequested();
            }

            FocusRing {
                shown: takePhoneButton.visualFocus
            }
        }

        Button {
            id: backButton

            text: qsTr("Back to editing")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: backButton.visualFocus
            }
        }
    }

    // The tab reason makes the button show its focus ring right away; giving the
    // focus up on closing makes the next opening set that reason again.
    onOpened: backButton.forceActiveFocus(Qt.TabFocusReason)
    onClosed: backButton.focus = false
    Component.onCompleted: Theme.calmPopup(root)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: root.explanation
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.gridUnit * 4

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1

                Label {
                    text: qsTr("Your version")
                    font.pixelSize: TypeScale.section
                    Accessible.role: Accessible.Heading
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        objectName: "ownVersionText"
                        text: root.ownText
                        wrapMode: Text.Wrap
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1

                Label {
                    text: qsTr("On the phone")
                    font.pixelSize: TypeScale.section
                    Accessible.role: Accessible.Heading
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        objectName: "phoneVersionText"
                        text: root.phoneText
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
