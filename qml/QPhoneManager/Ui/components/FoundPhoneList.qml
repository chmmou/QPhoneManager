pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property FoundPhoneModel phones
    required property string emptyText
    required property string listName

    signal chosen(string address)

    spacing: Theme.gridUnit

    Label {
        Layout.fillWidth: true
        visible: root.phones.count === 0
        text: root.emptyText
        color: Theme.foregroundOnSurfaceVariant
        wrapMode: Text.Wrap
    }

    ListView {
        id: list

        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(list.contentHeight, TypeScale.body * 14)
        visible: root.phones.count > 0
        // The list scrolls when more phones are found than fit.
        clip: true
        model: root.phones
        Accessible.role: Accessible.List
        Accessible.name: root.listName
        ScrollBar.vertical: ThemedScrollBar {}

        delegate: ItemDelegate {
            id: phoneDelegate

            required property string label
            required property string detail
            required property string address
            required property bool available

            width: ListView.view.width
            text: phoneDelegate.label
            enabled: phoneDelegate.available
            Accessible.description: phoneDelegate.detail
            onClicked: root.chosen(phoneDelegate.address)

            contentItem: ColumnLayout {
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: phoneDelegate.label
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: phoneDelegate.detail
                    font.pixelSize: TypeScale.hint
                    color: Theme.foregroundOnSurfaceVariant
                    elide: Text.ElideRight
                }
            }

            FocusRing {
                shown: phoneDelegate.activeFocus
            }
        }
    }
}
