pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    required property list<SectionEntry> sections
    required property int currentIndex
    property bool compact: false

    signal sectionSelected(int index)

    padding: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: sectionList

            Layout.fillWidth: true
            Layout.fillHeight: true
            // The list scrolls when the window is too low for all sections.
            clip: true
            model: root.sections
            currentIndex: root.currentIndex
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Sections")
            ScrollBar.vertical: ScrollBar {}

            delegate: SidebarButton {
                required property SectionEntry modelData
                required property int index

                width: ListView.view.width
                text: modelData.title
                iconName: modelData.iconName
                compact: root.compact
                highlighted: index === root.currentIndex
                objectName: "section-" + modelData.key
                onClicked: root.sectionSelected(index)
            }
        }

        MenuSeparator {
            Layout.fillWidth: true
        }

        SidebarButton {
            Layout.fillWidth: true
            objectName: "section-settings"
            text: qsTr("Settings")
            iconName: "settings"
            compact: root.compact
            enabled: false
        }

        SidebarButton {
            Layout.fillWidth: true
            objectName: "section-help"
            text: qsTr("Help")
            iconName: "help"
            compact: root.compact
            enabled: false
        }
    }
}
