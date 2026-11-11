import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string title: ""
    required property string text
    property string actionText: ""
    property string actionIcon: ""

    signal actionTriggered

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(root.width - Theme.gridUnit * 8, TypeScale.body * 28)
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            visible: root.title.length > 0
            text: root.title
            font.pixelSize: TypeScale.section
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            text: root.text
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Button {
            id: actionButton

            objectName: root.objectName.length > 0 ? root.objectName + "Action" : ""
            Layout.alignment: Qt.AlignHCenter
            visible: root.actionText.length > 0
            text: root.actionText
            icon.source: root.actionIcon.length > 0 ? Theme.icon(root.actionIcon) : ""
            onClicked: root.actionTriggered()

            FocusRing {
                shown: actionButton.visualFocus
            }
        }
    }
}
