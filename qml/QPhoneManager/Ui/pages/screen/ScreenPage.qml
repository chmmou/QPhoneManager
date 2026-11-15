pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    readonly property ScreenMirror mirror: AppController.screen
    required property string emptyText
    readonly property bool canSteer: root.mirror.active && root.mirror.canControl
    // Quarter turns of the picture, clockwise.
    property int turns: 0
    property bool fitToWindow: true
    property bool keyboardInput: false

    signal connectRequested

    objectName: "screenPage"

    // The phone is captured only while its screen is in view.
    onVisibleChanged: {
        if (!root.visible)
            root.mirror.stop();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: root.mirror.connected
            padding: Theme.gridUnit * 2
            Material.background: Theme.surface

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit

                Button {
                    id: startButton

                    objectName: "screenStartButton"
                    text: root.mirror.active || root.mirror.starting ? qsTr("Stop") : qsTr("Show the screen")
                    icon.source: Theme.icon(root.mirror.active || root.mirror.starting ? "player-stop" : "player-play")
                    onClicked: root.toggleMirroring()

                    FocusRing {
                        shown: startButton.activeFocus
                    }
                }

                ToolSeparator {}

                ToolButton {
                    id: backButton

                    objectName: "screenBackButton"
                    text: qsTr("Back")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("chevron-left")
                    enabled: root.canSteer
                    Accessible.name: backButton.text
                    ToolTip.visible: backButton.hovered
                    ToolTip.text: backButton.text
                    onClicked: root.mirror.key("back")

                    FocusRing {
                        shown: backButton.activeFocus
                    }
                }

                ToolButton {
                    id: homeButton

                    objectName: "screenHomeButton"
                    text: qsTr("Home screen")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("home")
                    enabled: root.canSteer
                    Accessible.name: homeButton.text
                    ToolTip.visible: homeButton.hovered
                    ToolTip.text: homeButton.text
                    onClicked: root.mirror.key("home")

                    FocusRing {
                        shown: homeButton.activeFocus
                    }
                }

                ToolButton {
                    id: recentsButton

                    objectName: "screenRecentsButton"
                    text: qsTr("Recent apps")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("square")
                    enabled: root.canSteer
                    Accessible.name: recentsButton.text
                    ToolTip.visible: recentsButton.hovered
                    ToolTip.text: recentsButton.text
                    onClicked: root.mirror.key("recents")

                    FocusRing {
                        shown: recentsButton.activeFocus
                    }
                }

                ToolButton {
                    id: keyboardButton

                    objectName: "screenKeyboardButton"
                    text: qsTr("Type on the phone")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("keyboard")
                    checkable: true
                    checked: root.keyboardInput
                    enabled: root.canSteer
                    Accessible.name: keyboardButton.text
                    ToolTip.visible: keyboardButton.hovered
                    ToolTip.text: keyboardButton.text
                    onToggled: root.setKeyboardInput(keyboardButton.checked)

                    // The style marks the checked state by the icon colour alone.
                    Rectangle {
                        objectName: "keyboardCheckedMarker"
                        anchors.fill: parent
                        anchors.margins: Theme.gridUnit
                        visible: keyboardButton.checked
                        radius: Theme.gridUnit
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.outline
                    }

                    FocusRing {
                        shown: keyboardButton.activeFocus
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ToolButton {
                    id: rotateButton

                    objectName: "screenRotateButton"
                    text: qsTr("Turn the picture")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("rotate-clockwise")
                    enabled: root.mirror.hasFrame
                    Accessible.name: rotateButton.text
                    ToolTip.visible: rotateButton.hovered
                    ToolTip.text: rotateButton.text
                    onClicked: root.turns = (root.turns + 1) % 4

                    FocusRing {
                        shown: rotateButton.activeFocus
                    }
                }

                ToolButton {
                    id: fitButton

                    objectName: "screenFitButton"
                    text: qsTr("Fit into the window")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("arrows-maximize")
                    checkable: true
                    checked: root.fitToWindow
                    enabled: root.mirror.hasFrame
                    Accessible.name: fitButton.text
                    Accessible.description: qsTr("Off shows the screen at its full size")
                    ToolTip.visible: fitButton.hovered
                    ToolTip.text: root.fitToWindow ? fitButton.text : qsTr("Full size, 100 %")
                    onToggled: root.fitToWindow = fitButton.checked

                    // The style marks the checked state by the icon colour alone.
                    Rectangle {
                        objectName: "fitCheckedMarker"
                        anchors.fill: parent
                        anchors.margins: Theme.gridUnit
                        visible: fitButton.checked
                        radius: Theme.gridUnit
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.outline
                    }

                    FocusRing {
                        shown: fitButton.activeFocus
                    }
                }

                ToolButton {
                    id: screenshotButton

                    objectName: "screenshotButton"
                    text: qsTr("Save a screenshot")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("camera")
                    enabled: root.mirror.hasFrame
                    Accessible.name: screenshotButton.text
                    ToolTip.visible: screenshotButton.hovered
                    ToolTip.text: screenshotButton.text
                    onClicked: screenshotDialog.open()

                    FocusRing {
                        shown: screenshotButton.activeFocus
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.mirror.connected
            color: Theme.outlineVariant
        }

        Pane {
            objectName: "screenControlHint"
            Layout.fillWidth: true
            visible: root.mirror.connected && root.mirror.hintText.length > 0
            padding: Theme.gridUnit * 3
            Material.background: Theme.surfaceVariant
            Material.foreground: Theme.foregroundOnSurface

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit * 3

                TintedIcon {
                    Layout.preferredWidth: Theme.sidebarIconSize
                    Layout.preferredHeight: Theme.sidebarIconSize
                    Layout.alignment: Qt.AlignTop
                    name: "info-circle"
                    color: Theme.foregroundOnSurface
                }

                Label {
                    objectName: "screenControlHintText"
                    Layout.fillWidth: true
                    text: root.mirror.hintText
                    wrapMode: Text.Wrap
                }

                Button {
                    id: settingsButton

                    objectName: "screenAccessibilityButton"
                    visible: root.mirror.control === ScreenMirror.ServiceOff
                    text: qsTr("Open the setting on the phone")
                    onClicked: root.mirror.showAccessibilitySettings()

                    FocusRing {
                        shown: settingsButton.activeFocus
                    }
                }

                Button {
                    id: checkButton

                    objectName: "screenCheckButton"
                    visible: root.mirror.control === ScreenMirror.ServiceOff
                    text: qsTr("Check again")
                    flat: true
                    onClicked: root.mirror.checkControl()

                    FocusRing {
                        shown: checkButton.activeFocus
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.mirror.connected

            Flickable {
                id: screenArea

                anchors.fill: parent
                visible: root.mirror.hasFrame
                // At full size the screen is larger than the page.
                clip: true
                // A drag on the picture is a swipe on the phone; the scroll bars move the view.
                interactive: false
                contentWidth: screenView.width
                contentHeight: screenView.height
                ScrollBar.vertical: ThemedScrollBar {}
                ScrollBar.horizontal: ThemedScrollBar {}

                ScreenMirrorItem {
                    id: screenView

                    objectName: "screenView"
                    width: root.fitToWindow ? screenArea.width : Math.max(screenArea.width, screenView.implicitWidth)
                    height: root.fitToWindow ? screenArea.height : Math.max(screenArea.height, screenView.implicitHeight)
                    mirror: root.mirror
                    turns: root.turns
                    keyboardInput: root.keyboardInput
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Screen of the phone")
                    Accessible.description: root.keyboardInput ? qsTr("Typing goes to the phone. Tab leaves the screen.") : qsTr("Click to tap, drag to swipe, right-click to hold.")

                    FocusRing {
                        objectName: "screenViewFocusRing"
                        shown: screenView.activeFocus
                    }
                }
            }

            ColumnLayout {
                objectName: "screenStarting"
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.gridUnit * 8, TypeScale.body * 28)
                visible: root.mirror.starting
                spacing: Theme.gridUnit * 3

                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: root.mirror.starting
                }

                Label {
                    objectName: "screenStatusText"
                    Layout.fillWidth: true
                    text: root.mirror.statusText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }

            EmptyState {
                objectName: "screenIdle"
                anchors.fill: parent
                visible: !root.mirror.hasFrame && !root.mirror.starting
                text: qsTr("The screen of the phone shows here. The phone asks you first whether it may share it.")
                actionText: qsTr("Show the screen")
                actionIcon: "player-play"
                onActionTriggered: root.mirror.start()
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.mirror.connected
            text: root.emptyText
            actionText: qsTr("Connect")
            actionIcon: "plug-connected"
            onActionTriggered: root.connectRequested()
        }
    }

    FileDialog {
        id: screenshotDialog

        objectName: "screenshotDialog"
        title: qsTr("Save the screenshot")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG pictures (*.png)")]
        defaultSuffix: "png"
        onAccepted: root.mirror.saveScreenshot(screenshotDialog.selectedFile)
    }

    Connections {
        target: root.mirror

        function onStateChanged(): void {
            if (!root.mirror.active)
                root.keyboardInput = false;
        }
    }

    function toggleMirroring(): void {
        if (root.mirror.active || root.mirror.starting)
            root.mirror.stop();
        else
            root.mirror.start();
    }

    function setKeyboardInput(enabled: bool): void {
        root.keyboardInput = enabled;
        // Typing reaches the phone from the picture, so the focus goes there.
        if (enabled)
            screenView.forceActiveFocus(Qt.OtherFocusReason);
    }
}
