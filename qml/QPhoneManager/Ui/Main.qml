pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: root

    readonly property bool connected: false
    readonly property bool hasData: false
    property int currentSection: 0

    signal connectRequested

    width: 1280
    height: 800
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: qsTr("QPhoneManager")
    font.pixelSize: TypeScale.body
    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.primary: Theme.primary
    Material.accent: Theme.primary
    Material.background: Theme.surface
    Material.foreground: Theme.foregroundOnSurface

    header: AppHeader {
        id: appHeader

        title: sectionCatalog.all[root.currentSection].title
        connected: root.connected
        canSearch: root.hasData
    }

    footer: StatusBar {
        connected: root.connected
    }

    Sections {
        id: sectionCatalog
    }

    AppShortcuts {
        sectionCount: sectionCatalog.all.length
        canSearch: root.hasData
        canSync: root.connected
        onSectionRequested: index => root.currentSection = index
        onSearchRequested: appHeader.focusSearch()
        onSyncRequested: appHeader.syncRequested()
        onQuitRequested: Qt.quit()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            id: sidebar

            objectName: "sidebar"
            Layout.fillHeight: true
            Layout.preferredWidth: sidebar.compact ? Theme.compactSidebarWidth : Theme.sidebarWidth
            sections: sectionCatalog.all
            currentIndex: root.currentSection
            compact: root.width < Theme.compactWindowWidth
            onSectionSelected: index => root.currentSection = index
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Theme.outline
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            DisconnectedBanner {
                Layout.fillWidth: true
                visible: !root.connected
                onConnectRequested: root.connectRequested()
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.currentSection

                Repeater {
                    model: sectionCatalog.all

                    delegate: SectionPage {
                        required property SectionEntry modelData

                        entry: modelData
                        onConnectRequested: root.connectRequested()
                    }
                }
            }
        }
    }
}
