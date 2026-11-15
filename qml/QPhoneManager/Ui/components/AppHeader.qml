import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ToolBar {
    id: root

    required property string title
    required property string iconName
    required property bool connected
    property bool canSearch: false
    readonly property alias searchText: searchField.text

    signal syncRequested
    signal connectRequested
    signal phonesRequested

    Material.foreground: Theme.foregroundOnSurface
    background: Rectangle {
        color: Theme.surfaceVariant
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.gridUnit * 2
        anchors.rightMargin: Theme.gridUnit * 2
        spacing: Theme.gridUnit * 3

        ThemedComboBox {
            id: deviceBox

            objectName: "deviceBox"
            Layout.preferredWidth: deviceBox.font.pixelSize * 12
            model: AppController.devices
            textRole: "displayName"
            valueRole: "deviceId"
            currentIndex: AppController.devices.count > 0 ? AppController.devices.indexOf(AppController.currentDevice) : -1
            enabled: deviceBox.count > 0
            displayText: deviceBox.currentIndex >= 0 ? deviceBox.currentText : qsTr("No phone")
            Accessible.name: qsTr("Phone")
            onActivated: AppController.selectDevice(deviceBox.currentValue)
            Component.onCompleted: Theme.calmPopup(deviceBox.popup)

            FocusRing {
                shown: deviceBox.activeFocus
            }
        }

        ToolButton {
            id: phonesButton

            objectName: "phonesButton"
            text: qsTr("Manage phones")
            display: AbstractButton.IconOnly
            icon.source: Theme.icon("users")
            Accessible.name: phonesButton.text
            ToolTip.visible: phonesButton.hovered
            ToolTip.text: phonesButton.text
            onClicked: root.phonesRequested()

            FocusRing {
                shown: phonesButton.activeFocus
            }
        }

        ToolSeparator {}

        TintedIcon {
            objectName: "sectionIcon"
            Layout.preferredWidth: Theme.sidebarIconSize
            Layout.preferredHeight: Theme.sidebarIconSize
            visible: root.iconName.length > 0
            name: root.iconName
            color: Theme.foregroundOnSurface
        }

        Label {
            Layout.fillWidth: true
            text: root.title
            elide: Text.ElideRight
            font.pixelSize: TypeScale.title
        }

        ThemedTextField {
            id: searchField

            objectName: "searchField"
            Layout.preferredWidth: searchField.font.pixelSize * 16
            placeholderText: qsTr("Search")
            enabled: root.canSearch
            Accessible.name: qsTr("Search")
        }

        ToolButton {
            id: syncButton

            objectName: "syncButton"
            text: qsTr("Sync")
            display: AbstractButton.IconOnly
            icon.source: Theme.icon("refresh")
            enabled: root.connected && !AppController.syncing
            Accessible.name: syncButton.text
            onClicked: root.syncRequested()

            FocusRing {
                shown: syncButton.activeFocus
            }

            SideToolTip {
                parent: syncButton
                visible: syncButton.hovered
                text: syncButton.text
            }
        }

        ToolButton {
            id: connectionButton

            objectName: "connectionButton"
            text: root.connected || AppController.connecting ? qsTr("Disconnect") : qsTr("Connect")
            display: AbstractButton.IconOnly
            icon.source: Theme.icon(root.connected || AppController.connecting ? "plug-connected-x" : "plug-connected")
            Accessible.name: connectionButton.text
            onClicked: {
                if (root.connected || AppController.connecting)
                    AppController.disconnectPhone();
                else
                    root.connectRequested();
            }

            FocusRing {
                shown: connectionButton.activeFocus
            }

            SideToolTip {
                parent: connectionButton
                visible: connectionButton.hovered
                text: connectionButton.text
            }
        }
    }

    // Beside its button rather than below it: at the top of the window there is
    // no room above, and below it covered the close button of a message bar.
    component SideToolTip: ToolTip {
        x: -width - Theme.gridUnit
        y: ((parent?.height ?? 0) - height) / 2
    }

    function focusSearch(): void {
        searchField.forceActiveFocus(Qt.ShortcutFocusReason);
    }

    function clearSearch(): void {
        searchField.clear();
    }
}
