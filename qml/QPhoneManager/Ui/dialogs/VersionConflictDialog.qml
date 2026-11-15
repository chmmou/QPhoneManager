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
        DialogButton {
            id: keepOwnButton

            objectName: "keepOwnButton"
            text: qsTr("Save my version")
            onClicked: {
                root.close();
                root.keepOwnRequested();
            }
        }

        DialogButton {
            id: takePhoneButton

            objectName: "takePhoneButton"
            text: qsTr("Keep the phone's version")
            onClicked: {
                root.close();
                root.takePhoneRequested();
            }
        }

        DialogButton {
            id: backButton

            text: qsTr("Back to editing")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    onOpened: backButton.forceActiveFocus(Qt.PopupFocusReason)
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

                ThemedScrollView {
                    id: ownScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: ownScroll.availableWidth - ownScroll.ScrollBar.vertical.width

                    Label {
                        objectName: "ownVersionText"
                        width: ownScroll.contentWidth
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

                ThemedScrollView {
                    id: phoneScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: phoneScroll.availableWidth - phoneScroll.ScrollBar.vertical.width

                    Label {
                        objectName: "phoneVersionText"
                        width: phoneScroll.contentWidth
                        text: root.phoneText
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
