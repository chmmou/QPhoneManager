import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: root

    property string text: ""
    property bool isError: true
    /// Milliseconds a message stays before it closes on its own.
    property int autoCloseInterval: 15000
    /// The time to close runs; it waits while the pointer rests on the bar
    /// or the keyboard focus is in it, and starts anew after.
    readonly property bool closing: closeTimer.running

    visible: root.text.length > 0
    padding: Theme.gridUnit * 3
    // On a desktop without hover effects the bar still has to notice the pointer.
    hoverEnabled: true
    Material.background: root.isError ? Theme.error : Theme.surfaceVariant
    Material.foreground: root.isError ? Theme.foregroundOnError : Theme.foregroundOnSurface

    onTextChanged: {
        if (root.text.length > 0)
            messageLabel.Accessible.announce(root.text);
    }

    // Closing on its own limits the time to read a message, which the
    // accessibility guidelines want adjustable. Waiting while the pointer
    // rests on the bar or the focus is in it gives that time back.
    Timer {
        id: closeTimer

        interval: root.autoCloseInterval
        running: root.text.length > 0 && !root.hovered && !closeButton.activeFocus
        onTriggered: root.text = ""
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
                shown: closeButton.activeFocus
            }
        }
    }

    function show(message: string, error: bool): void {
        root.isError = error;
        root.text = "";
        root.text = message;
    }
}
