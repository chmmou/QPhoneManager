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

        ComboBox {
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

            FocusRing {
                shown: deviceBox.visualFocus
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
                shown: phonesButton.visualFocus
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

        TextField {
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
            ToolTip.visible: syncButton.hovered
            ToolTip.text: syncButton.text
            onClicked: root.syncRequested()

            FocusRing {
                shown: syncButton.visualFocus
            }
        }

        ToolButton {
            id: connectionButton

            objectName: "connectionButton"
            text: root.connected || AppController.connecting ? qsTr("Disconnect") : qsTr("Connect")
            display: AbstractButton.IconOnly
            icon.source: Theme.icon(root.connected || AppController.connecting ? "plug-connected-x" : "plug-connected")
            Accessible.name: connectionButton.text
            ToolTip.visible: connectionButton.hovered
            ToolTip.text: connectionButton.text
            onClicked: {
                if (root.connected || AppController.connecting)
                    AppController.disconnectPhone();
                else
                    root.connectRequested();
            }

            FocusRing {
                shown: connectionButton.visualFocus
            }
        }
    }

    function focusSearch(): void {
        searchField.forceActiveFocus(Qt.ShortcutFocusReason);
    }

    function clearSearch(): void {
        searchField.clear();
    }
}
