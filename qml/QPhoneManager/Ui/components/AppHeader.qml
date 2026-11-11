import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ToolBar {
    id: root

    required property string title
    required property bool connected
    property bool canSearch: false

    signal syncRequested

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
            model: []
            enabled: deviceBox.count > 0
            displayText: deviceBox.count > 0 ? deviceBox.currentText : qsTr("No phone")
            Accessible.name: qsTr("Phone")

            FocusRing {
                shown: deviceBox.visualFocus
            }
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
            enabled: root.connected
            Accessible.name: syncButton.text
            ToolTip.visible: syncButton.hovered
            ToolTip.text: syncButton.text
            onClicked: root.syncRequested()

            FocusRing {
                shown: syncButton.visualFocus
            }
        }
    }

    function focusSearch(): void {
        searchField.forceActiveFocus(Qt.ShortcutFocusReason);
    }
}
