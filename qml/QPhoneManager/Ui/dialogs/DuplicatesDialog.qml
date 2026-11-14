pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    required property ContactsArea area

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 40, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 40)
    height: Math.min(TypeScale.body * 32, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 32)
    modal: true
    Overlay.modal: Scrim {}
    Material.elevation: 0
    title: qsTr("Duplicate contacts")

    footer: DialogButtonBox {
        DialogButton {
            id: closeButton

            objectName: "duplicatesCloseButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    // The tab reason makes the button show its focus ring right away; giving the
    // focus up on closing makes the next opening set that reason again.
    onOpened: closeButton.forceActiveFocus(Qt.TabFocusReason)
    onClosed: closeButton.focus = false
    Component.onCompleted: Theme.calmPopup(root)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: root.area.busy ? qsTr("Looking for duplicates.") : (root.area.duplicates.count === 0 ? qsTr("No duplicates found.") : qsTr("Merging keeps the first contact, adds what only the second one has and deletes the second one on the phone."))
            wrapMode: Text.Wrap
        }

        ListView {
            id: list

            objectName: "duplicateList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The list scrolls when there are more pairs than fit.
            clip: true
            model: root.area.duplicates
            spacing: Theme.gridUnit * 2
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Duplicate contacts")
            ScrollBar.vertical: ThemedScrollBar {}

            delegate: RowLayout {
                id: pairRow

                required property int index
                required property string firstName
                required property string secondName
                required property string reason

                width: ListView.view.width
                spacing: Theme.gridUnit * 2

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("%1 and %2").arg(pairRow.firstName).arg(pairRow.secondName)
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: pairRow.reason
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }
                }

                Button {
                    id: mergeButton

                    text: qsTr("Merge")
                    flat: true
                    enabled: root.area.canEdit && !root.area.busy
                    Accessible.name: qsTr("Merge %1 and %2").arg(pairRow.firstName).arg(pairRow.secondName)
                    onClicked: root.area.mergeDuplicate(pairRow.index)

                    FocusRing {
                        shown: mergeButton.visualFocus
                    }
                }

                Button {
                    id: ignoreButton

                    text: qsTr("Ignore")
                    flat: true
                    Accessible.name: qsTr("Ignore %1 and %2").arg(pairRow.firstName).arg(pairRow.secondName)
                    onClicked: root.area.ignoreDuplicate(pairRow.index)

                    FocusRing {
                        shown: ignoreButton.visualFocus
                    }
                }
            }
        }
    }
}
