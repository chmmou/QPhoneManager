import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: root

    property string text: ""
    property bool isError: true

    visible: root.text.length > 0
    padding: Theme.gridUnit * 3
    Material.background: root.isError ? Theme.error : Theme.surfaceVariant
    Material.foreground: root.isError ? Theme.foregroundOnError : Theme.foregroundOnSurface

    onTextChanged: {
        if (root.text.length > 0)
            messageLabel.Accessible.announce(root.text);
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        TintedIcon {
            Layout.preferredWidth: Theme.listIconSize
            Layout.preferredHeight: Theme.listIconSize
            name: root.isError ? "alert-triangle" : "info-circle"
            color: root.isError ? Theme.foregroundOnError : Theme.foregroundOnSurface
        }

        Label {
            id: messageLabel

            objectName: "messageText"
            Layout.fillWidth: true
            text: root.text
            wrapMode: Text.Wrap
            Accessible.role: root.isError ? Accessible.AlertMessage : Accessible.StaticText
        }

        ToolButton {
            id: closeButton

            objectName: "messageCloseButton"
            text: qsTr("Close message")
            display: AbstractButton.IconOnly
            icon.source: Theme.icon("x")
            Accessible.name: closeButton.text
            ToolTip.visible: closeButton.hovered
            ToolTip.text: closeButton.text
            onClicked: root.text = ""

            FocusRing {
                shown: closeButton.visualFocus
            }
        }
    }

    function show(message: string, error: bool): void {
        root.isError = error;
        root.text = "";
        root.text = message;
    }
}
