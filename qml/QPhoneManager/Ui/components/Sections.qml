import QtQuick

QtObject {
    id: root

    readonly property list<SectionEntry> all: [
        SectionEntry {
            key: "overview"
            title: qsTr("Overview")
            iconName: "home"
            emptyTitle: qsTr("No phone connected.")
            emptyText: qsTr("Connect your phone over Wi-Fi, USB or Bluetooth.")
            hasList: false
        },
        SectionEntry {
            key: "contacts"
            title: qsTr("Contacts")
            iconName: "address-book"
            emptyText: qsTr("No contacts yet. Connect your phone to load them.")
        },
        SectionEntry {
            key: "calls"
            title: qsTr("Calls")
            iconName: "phone"
            emptyText: qsTr("No calls yet. Connect your phone to load the call log.")
        },
        SectionEntry {
            key: "messages"
            title: qsTr("Messages")
            iconName: "messages"
            emptyText: qsTr("No messages yet. Connect your phone to load them.")
        },
        SectionEntry {
            key: "calendar"
            title: qsTr("Calendar")
            iconName: "calendar"
            emptyText: qsTr("No appointments yet. Connect your phone to load the calendar.")
        },
        SectionEntry {
            key: "tasks"
            title: qsTr("Tasks")
            iconName: "checklist"
            emptyText: qsTr("No tasks yet. Connect your phone to load them.")
        },
        SectionEntry {
            key: "notes"
            title: qsTr("Notes")
            iconName: "notes"
            emptyText: qsTr("No notes yet. Connect your phone to load them.")
        },
        SectionEntry {
            key: "files"
            title: qsTr("Files")
            iconName: "folder"
            emptyText: qsTr("Connect your phone to browse its files.")
        },
        SectionEntry {
            key: "apps"
            title: qsTr("Apps")
            iconName: "apps"
            emptyText: qsTr("Connect your phone to see its apps.")
        },
        SectionEntry {
            key: "screen"
            title: qsTr("Screen")
            iconName: "screen-share"
            emptyText: qsTr("Connect your phone to show its screen here.")
            hasList: false
        },
        SectionEntry {
            key: "notifications"
            title: qsTr("Notifications")
            iconName: "bell"
            emptyText: qsTr("Connect your phone to see its notifications.")
        }
    ]
}
