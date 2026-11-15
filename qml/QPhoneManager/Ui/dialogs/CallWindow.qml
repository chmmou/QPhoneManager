import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Window {
    id: root

    readonly property CallControl call: AppController.call
    readonly property bool ringing: root.call.state === CallControl.Ringing
    readonly property bool talking: root.call.state === CallControl.Active

    width: Math.max(TypeScale.body * 26, content.implicitWidth + Theme.gridUnit * 12)
    height: content.implicitHeight + Theme.gridUnit * 12
    // A call has to be seen even when the main window is hidden or covered.
    flags: Qt.Dialog | Qt.WindowStaysOnTopHint
    title: qsTr("Call")
    color: Theme.surface
    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.primary: Theme.primary
    Material.accent: Theme.primary
    Material.background: Theme.surface
    Material.foreground: Theme.foregroundOnSurface

    // The focus that the window gets on showing up is shown as well, so that
    // it is clear which button Enter presses.
    onActiveChanged: {
        if (root.active)
            root.focusFirstButton();
    }

    // The window follows the call; closing it by hand does not end the call,
    // so it shows again with the next one.
    Connections {
        target: root.call

        function onCallChanged(): void {
            if (root.call.active && !root.visible) {
                root.show();
                root.raise();
                root.requestActivate();
            } else if (!root.call.active && root.visible) {
                // A button that kept its focus would keep the reason of then
                // and show no ring on the next call.
                answerButton.focus = false;
                hangUpButton.focus = false;
                root.hide();
            } else if (root.visible) {
                root.focusFirstButton();
            }
        }
    }

    ColumnLayout {
        id: content

        anchors.centerIn: parent
        spacing: Theme.gridUnit * 4

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.gridUnit * 3

            TintedIcon {
                Layout.preferredWidth: Theme.sidebarIconSize
                Layout.preferredHeight: Theme.sidebarIconSize
                name: root.call.state === CallControl.Dialling ? "phone-outgoing" : "phone-incoming"
                color: Theme.primary
            }

            Label {
                objectName: "callStatusText"
                text: root.call.statusText
                color: Theme.foregroundOnSurfaceVariant
            }
        }

        Label {
            objectName: "callCallerText"
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: TypeScale.body * 30
            text: root.call.callerText
            font.pixelSize: TypeScale.title
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }

        Label {
            objectName: "callNumberText"
            Layout.alignment: Qt.AlignHCenter
            visible: root.call.numberText.length > 0
            text: root.call.numberText
            color: Theme.foregroundOnSurfaceVariant
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.gridUnit * 3

            PrimaryButton {
                id: answerButton

                objectName: "callAnswerButton"
                text: qsTr("Answer")
                icon.source: Theme.icon("phone-call")
                enabled: root.ringing
                onClicked: root.call.answer()

                FocusRing {
                    shown: answerButton.activeFocus
                }
            }

            Button {
                id: hangUpButton

                objectName: "callHangUpButton"
                text: root.ringing ? qsTr("Decline") : qsTr("Hang up")
                icon.source: Theme.icon("phone-off")
                enabled: root.call.active
                onClicked: root.call.hangUp()

                FocusRing {
                    shown: hangUpButton.activeFocus
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.gridUnit * 3

            PrimaryButton {
                id: muteButton

                objectName: "callMuteButton"
                text: qsTr("Mute")
                icon.source: Theme.icon("microphone-off")
                flat: !root.call.muted
                enabled: root.talking
                Accessible.checkable: true
                Accessible.checked: root.call.muted
                onClicked: root.call.setMuted(!root.call.muted)

                FocusRing {
                    shown: muteButton.activeFocus
                }
            }

            PrimaryButton {
                id: speakerButton

                objectName: "callSpeakerButton"
                text: qsTr("Speaker")
                icon.source: Theme.icon("volume")
                flat: !root.call.speakerOn
                enabled: root.talking
                Accessible.checkable: true
                Accessible.checked: root.call.speakerOn
                onClicked: root.call.setSpeakerOn(!root.call.speakerOn)

                FocusRing {
                    shown: speakerButton.activeFocus
                }
            }
        }
    }

    function focusFirstButton(): void {
        if (root.ringing)
            answerButton.forceActiveFocus(Qt.OtherFocusReason);
        else
            hangUpButton.forceActiveFocus(Qt.OtherFocusReason);
    }
}
