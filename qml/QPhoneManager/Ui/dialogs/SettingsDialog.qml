pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: root

    parent: Overlay.overlay
    anchors.centerIn: parent
    // Wide enough for the four tab labels in both languages.
    width: Math.min(TypeScale.body * 46, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 46)
    height: Math.min(TypeScale.body * 38, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 38)
    modal: true
    Overlay.modal: Scrim {}
    // The raised look of Material draws its dialog through a shader effect,
    // which the software renderer leaves out together with the background.
    Material.elevation: 0
    title: qsTr("Settings")

    footer: DialogButtonBox {
        Button {
            id: closeButton

            objectName: "settingsCloseButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: closeButton.visualFocus
            }
        }
    }

    onOpened: {
        adbBox.model = AppController.adbSuggestions();
        adbBox.editText = Settings.adbPath;
        adbBox.forceActiveFocus(Qt.PopupFocusReason);
    }

    // The shortcut of the window does not reach past a modal dialog, so F1
    // asks for the chapter here. Off while closed, so the two never compete.
    Shortcut {
        sequences: [StandardKey.HelpContents]
        enabled: root.opened
        onActivated: root.helpRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        TabBar {
            id: tabs

            objectName: "settingsTabs"
            Layout.fillWidth: true

            TabButton {
                id: connectionTab

                width: implicitWidth
                text: qsTr("Connection")

                FocusRing {
                    shown: connectionTab.visualFocus
                }
            }

            TabButton {
                id: appearanceTab

                width: implicitWidth
                text: qsTr("Appearance")

                FocusRing {
                    shown: appearanceTab.visualFocus
                }
            }

            TabButton {
                id: storageTab

                width: implicitWidth
                text: qsTr("Language and storage")

                FocusRing {
                    shown: storageTab.visualFocus
                }
            }

            TabButton {
                id: notificationsTab

                objectName: "settingsNotificationsTab"
                width: implicitWidth
                text: qsTr("Notifications")

                FocusRing {
                    shown: notificationsTab.visualFocus
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ScrollView {
                id: connectionPage

                contentWidth: connectionPage.availableWidth

                ColumnLayout {
                    width: connectionPage.availableWidth
                    spacing: Theme.gridUnit * 3

                    Label {
                        text: qsTr("adb program for USB")
                        font.pixelSize: TypeScale.section
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gridUnit * 2

                        ComboBox {
                            id: adbBox

                            objectName: "adbPathBox"
                            Layout.fillWidth: true
                            editable: true
                            Accessible.name: qsTr("Path of the adb program")
                            onAccepted: Settings.adbPath = adbBox.editText.trim()
                            onActivated: Settings.adbPath = adbBox.currentText
                            onActiveFocusChanged: {
                                if (!adbBox.activeFocus)
                                    Settings.adbPath = adbBox.editText.trim();
                            }
                        }

                        Button {
                            id: chooseAdbButton

                            text: qsTr("Choose")
                            onClicked: adbFileDialog.open()

                            FocusRing {
                                shown: chooseAdbButton.visualFocus
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: Settings.adbPath.length === 0 && adbBox.count > 0 ? qsTr("Empty means: use %1.").arg(adbBox.textAt(0)) : (adbBox.count === 0 ? qsTr("adb was not found on this computer. Install the Android platform tools, then choose adb here.") : "")
                        visible: text.length > 0
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        wrapMode: Text.Wrap
                    }

                    Label {
                        text: qsTr("Order of the connection ways")
                        font.pixelSize: TypeScale.section
                    }

                    Repeater {
                        model: Settings.connectionOrder

                        delegate: RowLayout {
                            id: orderRow

                            required property string modelData
                            required property int index

                            Layout.fillWidth: true
                            spacing: Theme.gridUnit * 2

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("%1. %2").arg(orderRow.index + 1).arg(root.wayName(orderRow.modelData))
                            }

                            Button {
                                id: earlierButton

                                text: qsTr("Earlier")
                                flat: true
                                enabled: orderRow.index > 0
                                Accessible.name: qsTr("Try %1 earlier").arg(root.wayName(orderRow.modelData))
                                onClicked: root.moveWay(orderRow.index, -1)

                                FocusRing {
                                    shown: earlierButton.visualFocus
                                }
                            }

                            Button {
                                id: laterButton

                                text: qsTr("Later")
                                flat: true
                                enabled: orderRow.index < Settings.connectionOrder.length - 1
                                Accessible.name: qsTr("Try %1 later").arg(root.wayName(orderRow.modelData))
                                onClicked: root.moveWay(orderRow.index, 1)

                                FocusRing {
                                    shown: laterButton.visualFocus
                                }
                            }
                        }
                    }

                    CheckBox {
                        objectName: "autoConnectBox"
                        Layout.fillWidth: true
                        text: qsTr("Connect to the last phone at start and after a lost connection")
                        checked: Settings.autoConnect
                        onToggled: Settings.autoConnect = checked
                    }

                    CheckBox {
                        objectName: "listenBox"
                        Layout.fillWidth: true
                        text: qsTr("Let the phone connect to this computer at any time")
                        checked: Settings.listenForPhone
                        onToggled: Settings.listenForPhone = checked
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Without this, this computer waits for the phone only while the QR code is shown.")
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        wrapMode: Text.Wrap
                    }

                    Label {
                        objectName: "settingsWifiNotice"
                        Layout.fillWidth: true
                        text: qsTr("Wi-Fi connections are not encrypted. Use them only in a network you trust, or connect over USB.")
                        wrapMode: Text.Wrap
                    }
                }
            }

            ColumnLayout {
                spacing: Theme.gridUnit * 3

                Label {
                    text: qsTr("Colors")
                    font.pixelSize: TypeScale.section
                }

                ButtonGroup {
                    id: schemeGroup
                }

                RadioButton {
                    text: qsTr("Like the system")
                    ButtonGroup.group: schemeGroup
                    checked: Settings.colorScheme === Settings.ColorScheme.System
                    onToggled: Settings.colorScheme = Settings.ColorScheme.System
                }

                RadioButton {
                    text: qsTr("Light")
                    ButtonGroup.group: schemeGroup
                    checked: Settings.colorScheme === Settings.ColorScheme.Light
                    onToggled: Settings.colorScheme = Settings.ColorScheme.Light
                }

                RadioButton {
                    text: qsTr("Dark")
                    ButtonGroup.group: schemeGroup
                    checked: Settings.colorScheme === Settings.ColorScheme.Dark
                    onToggled: Settings.colorScheme = Settings.ColorScheme.Dark
                }

                CheckBox {
                    Layout.fillWidth: true
                    text: qsTr("Reduce animations")
                    checked: Settings.reduceMotion
                    onToggled: Settings.reduceMotion = checked
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                spacing: Theme.gridUnit * 3

                Label {
                    text: qsTr("Language")
                    font.pixelSize: TypeScale.section
                }

                ComboBox {
                    id: languageBox

                    objectName: "languageBox"
                    Layout.preferredWidth: TypeScale.body * 16
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        {
                            text: qsTr("Like the system"),
                            value: ""
                        },
                        {
                            text: "Deutsch",
                            value: "de"
                        },
                        {
                            text: "English",
                            value: "en"
                        }
                    ]
                    // The binding first runs before the model is filled, when no
                    // value is found yet; reading the count runs it again once it is.
                    currentIndex: languageBox.count > 0 ? languageBox.indexOfValue(Settings.language) : -1
                    Accessible.name: qsTr("Language")
                    onActivated: Settings.language = languageBox.currentValue
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("A new language applies after QPhoneManager restarts.")
                    font.pixelSize: TypeScale.hint
                    color: Theme.foregroundOnSurfaceVariant
                    wrapMode: Text.Wrap
                }

                Label {
                    text: qsTr("Storage folder")
                    font.pixelSize: TypeScale.section
                }

                Label {
                    objectName: "storageFolder"
                    Layout.fillWidth: true
                    text: AppController.profilesDirectory
                    wrapMode: Text.WrapAnywhere
                }

                RowLayout {
                    spacing: Theme.gridUnit * 2

                    Button {
                        id: chooseFolderButton

                        text: qsTr("Choose folder")
                        onClicked: folderDialog.open()

                        FocusRing {
                            shown: chooseFolderButton.visualFocus
                        }
                    }

                    Button {
                        id: defaultFolderButton

                        text: qsTr("Use default folder")
                        enabled: Settings.dataDirectory.length > 0
                        onClicked: Settings.dataDirectory = ""

                        FocusRing {
                            shown: defaultFolderButton.visualFocus
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Data that is already stored stays in the old folder.")
                    font.pixelSize: TypeScale.hint
                    color: Theme.foregroundOnSurfaceVariant
                    wrapMode: Text.Wrap
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                objectName: "settingsNotificationsPage"
                spacing: Theme.gridUnit * 3

                Label {
                    text: qsTr("Phone and desktop")
                    font.pixelSize: TypeScale.section
                }

                CheckBox {
                    objectName: "desktopNotificationsBox"
                    Layout.fillWidth: true
                    text: qsTr("Show new notifications of the phone on this computer")
                    checked: Settings.notificationsEnabled
                    onToggled: Settings.notificationsEnabled = checked
                }

                CheckBox {
                    objectName: "clipboardSyncBox"
                    Layout.fillWidth: true
                    text: qsTr("Share the clipboard between the phone and this computer")
                    checked: Settings.clipboardSync
                    onToggled: Settings.clipboardSync = checked
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Text you copy on this computer goes to the phone. Android passes on text copied on the phone only while MyPhoneExplorer Client is open there.")
                    font.pixelSize: TypeScale.hint
                    color: Theme.foregroundOnSurfaceVariant
                    wrapMode: Text.Wrap
                }

                Label {
                    text: qsTr("Start")
                    font.pixelSize: TypeScale.section
                }

                CheckBox {
                    objectName: "startMinimizedBox"
                    Layout.fillWidth: true
                    text: qsTr("Start in the notification area, without the window")
                    checked: Settings.startMinimized
                    onToggled: Settings.startMinimized = checked
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("If the desktop has no notification area, the window opens anyway. This applies from the next start.")
                    font.pixelSize: TypeScale.hint
                    color: Theme.foregroundOnSurfaceVariant
                    wrapMode: Text.Wrap
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    FileDialog {
        id: adbFileDialog

        title: qsTr("Choose the adb program")
        fileMode: FileDialog.OpenFile
        onAccepted: {
            const path = Settings.pathFromUrl(adbFileDialog.selectedFile);
            if (path.length > 0) {
                Settings.adbPath = path;
                adbBox.editText = Settings.adbPath;
            }
        }
    }

    FolderDialog {
        id: folderDialog

        title: qsTr("Choose the storage folder")
        onAccepted: {
            const path = Settings.pathFromUrl(folderDialog.selectedFolder);
            if (path.length > 0)
                Settings.dataDirectory = path;
        }
    }

    function wayName(key: string): string {
        if (key === "usb")
            return qsTr("USB cable");
        if (key === "wifi")
            return qsTr("Wi-Fi");
        return qsTr("Bluetooth");
    }

    function moveWay(index: int, step: int): void {
        const order = Settings.connectionOrder.slice();
        const target = index + step;
        if (target < 0 || target >= order.length)
            return;
        const moved = order[index];
        order[index] = order[target];
        order[target] = moved;
        Settings.connectionOrder = order;
    }
}
