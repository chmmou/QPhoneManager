pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    required property list<SectionEntry> sections
    required property int currentIndex
    property bool compact: false
    /// Width at which the longest label fits next to its icon.
    readonly property int fittingWidth: Math.ceil(root.widestLabel(labelMetrics.font.pixelSize) + settingsButton.leftPadding + settingsButton.rightPadding + settingsButton.spacing + Theme.sidebarIconSize + Theme.gridUnit * 4)

    signal sectionSelected(int index)
    signal settingsRequested

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
            id: settingsButton

            Layout.fillWidth: true
            objectName: "section-settings"
            text: qsTr("Settings")
            iconName: "settings"
            compact: root.compact
            onClicked: root.settingsRequested()
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

    FontMetrics {
        id: labelMetrics

        font: settingsButton.font
    }

    // The font size is passed in so that the width follows the font; a method
    // call alone does not make the binding depend on it.
    function widestLabel(pixelSize: int): real {
        if (pixelSize <= 0)
            return 0;
        let widest = Math.max(labelMetrics.advanceWidth(qsTr("Settings")), labelMetrics.advanceWidth(qsTr("Help")));
        for (let i = 0; i < root.sections.length; ++i)
            widest = Math.max(widest, labelMetrics.advanceWidth(root.sections[i].title));
        return widest;
    }
}
