pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property SectionEntry entry

    signal connectRequested

    objectName: "page-" + root.entry.key

    Component {
        id: placeholder

        EmptyState {
            title: root.entry.emptyTitle
            text: root.entry.emptyText
            actionText: qsTr("Connect")
            actionIcon: "plug-connected"
            onActionTriggered: root.connectRequested()
        }
    }

    Component {
        id: listAndDetail

        ListDetailView {
            listComponent: placeholder
            detailComponent: Component {
                EmptyState {
                    text: qsTr("Select an entry in the list.")
                }
            }
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.entry.hasList ? listAndDetail : placeholder
    }
}
