pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    readonly property ComponentRow currentRow: componentList.currentItem as ComponentRow

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 46, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 46)
    height: Math.min(TypeScale.body * 40, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 40)
    modal: true
    Overlay.modal: Scrim {}
    // The raised look of Material draws its dialog through a shader effect,
    // which the software renderer leaves out together with the background.
    Material.elevation: 0
    title: qsTr("About QPhoneManager")

    footer: DialogButtonBox {
        Button {
            id: closeButton

            objectName: "aboutCloseButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: closeButton.visualFocus
            }
        }
    }

    onOpened: {
        componentList.currentIndex = 0;
        componentList.forceActiveFocus(Qt.TabFocusReason);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gridUnit * 4

            Image {
                Layout.preferredWidth: Theme.sidebarIconSize * 2
                Layout.preferredHeight: Theme.sidebarIconSize * 2
                Layout.alignment: Qt.AlignTop
                source: "qrc:/icons/app/qphonemanager.svg"
                sourceSize.width: Theme.sidebarIconSize * 2
                sourceSize.height: Theme.sidebarIconSize * 2
                Accessible.ignored: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.gridUnit

                Label {
                    objectName: "aboutVersion"
                    Layout.fillWidth: true
                    text: qsTr("QPhoneManager %1").arg(AboutInfo.version)
                    font.pixelSize: TypeScale.section
                    wrapMode: Text.Wrap
                }

                Label {
                    objectName: "aboutCopyright"
                    Layout.fillWidth: true
                    text: AboutInfo.copyright
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }

                Label {
                    objectName: "aboutLicense"
                    Layout.fillWidth: true
                    text: qsTr("License: %1").arg(AboutInfo.license)
                    wrapMode: Text.Wrap
                }

                Label {
                    objectName: "aboutQtVersion"
                    Layout.fillWidth: true
                    text: qsTr("Runs on Qt %1").arg(AboutInfo.qtVersion)
                    color: Theme.foregroundOnSurfaceVariant
                    wrapMode: Text.Wrap
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Components and their licenses")
            font.pixelSize: TypeScale.section
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.gridUnit * 3

            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: TypeScale.body * 15

                ListView {
                    id: componentList

                    objectName: "componentList"
                    anchors.fill: parent
                    // The list scrolls when the dialog is too low for all components.
                    clip: true
                    model: AboutInfo
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    highlightMoveDuration: 0
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Components")
                    Accessible.description: qsTr("The arrow keys choose a component. Its license shows next to the list.")
                    ScrollBar.vertical: ScrollBar {}

                    delegate: ComponentRow {
                        width: ListView.view.width
                    }
                }

                FocusRing {
                    objectName: "componentListFocusRing"
                    shown: componentList.activeFocus
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    id: licenseScroll

                    anchors.fill: parent
                    contentWidth: licenseScroll.availableWidth

                    TextArea {
                        id: licenseText

                        objectName: "licenseText"
                        readOnly: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        selectByKeyboard: true
                        activeFocusOnTab: true
                        font.pixelSize: TypeScale.hint
                        text: root.currentRow ? root.currentRow.licenseText : ""
                        background: null
                        Accessible.name: root.currentRow ? qsTr("License of %1").arg(root.currentRow.name) : ""
                    }
                }

                FocusRing {
                    objectName: "licenseTextFocusRing"
                    shown: licenseText.activeFocus
                }
            }
        }
    }

    // One component with its version, license and what it is used for. A
    // plain row: the list carries the focus and moves through the rows.
    component ComponentRow: Rectangle {
        id: row

        required property int index
        required property string name
        required property string version
        required property string license
        required property string purpose
        required property string licenseText

        implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 4
        color: row.ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
        Accessible.role: Accessible.ListItem
        Accessible.name: qsTr("%1 %2, %3").arg(row.name).arg(row.version).arg(row.license)
        Accessible.description: row.purpose

        TapHandler {
            onTapped: row.ListView.view.currentIndex = row.index
        }

        ColumnLayout {
            id: rowContent

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.gridUnit * 4
            anchors.rightMargin: Theme.gridUnit * 4
            spacing: 0

            Label {
                Layout.fillWidth: true
                text: row.name
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("%1, %2").arg(row.version).arg(row.license)
                textFormat: Text.PlainText
                font.pixelSize: TypeScale.hint
                color: Theme.foregroundOnSurfaceVariant
                elide: Text.ElideRight
            }
        }
    }
}
