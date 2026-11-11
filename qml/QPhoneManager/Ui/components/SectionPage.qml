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

    Component {
        id: overview

        OverviewPage {
            onConnectRequested: root.connectRequested()
        }
    }

    Component {
        id: contacts

        ContactsPage {
            emptyText: root.entry.emptyText
            onConnectRequested: root.connectRequested()
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: {
            if (root.entry.key === "overview")
                return overview;
            if (root.entry.key === "contacts")
                return contacts;
            return root.entry.hasList ? listAndDetail : placeholder;
        }
    }
}
