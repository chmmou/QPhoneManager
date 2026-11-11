pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: root

    readonly property bool connected: AppController.connected
    readonly property string currentKey: sectionCatalog.all[root.currentSection].key
    readonly property bool hasData: (root.currentKey === "contacts" && AppController.contacts.hasPhone) || (root.currentKey === "calls" && AppController.calls.hasPhone) || (root.currentKey === "messages" && AppController.messages.hasPhone)
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
        iconName: sectionCatalog.all[root.currentSection].iconName
        connected: root.connected
        canSearch: root.hasData
        onSyncRequested: AppController.syncAll()
        onConnectRequested: root.connectRequested()
        onPhonesRequested: profilesDialog.open()
    }

    footer: StatusBar {}

    onConnectRequested: connectDialog.open()
    onCurrentSectionChanged: appHeader.clearSearch()
    Component.onCompleted: AppController.start()

    Binding {
        target: AppController.contacts
        property: "searchText"
        value: root.currentKey === "contacts" ? appHeader.searchText : ""
    }

    Connections {
        target: AppController.contacts

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }

        function onMessageRequested(number: string): void {
            if (AppController.messages.composeTo(number))
                root.showSection("messages");
        }
    }

    Binding {
        target: AppController.calls
        property: "searchText"
        value: root.currentKey === "calls" ? appHeader.searchText : ""
    }

    Connections {
        target: AppController.calls

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }

        function onMessageRequested(number: string): void {
            if (AppController.messages.composeTo(number))
                root.showSection("messages");
        }

        function onNewContactRequested(name: string, number: string): void {
            if (AppController.contacts.startNewContact(name, number))
                root.showSection("contacts");
        }
    }

    Binding {
        target: AppController.messages
        property: "searchText"
        value: root.currentKey === "messages" ? appHeader.searchText : ""
    }

    Connections {
        target: AppController.messages

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }
    }

    Connections {
        target: AppController

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onSyncFinished(summary: string): void {
            messageBar.show(summary, false);
        }

        function onPinRequested(remainingTries: int, wrongPin: bool): void {
            pinDialog.remainingTries = remainingTries;
            pinDialog.wrongPin = wrongPin;
            pinDialog.open();
        }

        function onConnectionStateChanged(): void {
            if (pinDialog.opened && !AppController.connecting)
                pinDialog.close();
            if (AppController.connected && messageBar.isError)
                messageBar.text = "";
        }
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
        onSettingsRequested: settingsDialog.open()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            id: sidebar

            objectName: "sidebar"
            Layout.fillHeight: true
            Layout.preferredWidth: sidebar.compact ? Theme.compactSidebarWidth : Math.max(Theme.sidebarWidth, sidebar.fittingWidth)
            sections: sectionCatalog.all
            currentIndex: root.currentSection
            compact: root.width < Theme.compactWindowWidth
            onSectionSelected: index => root.currentSection = index
            onSettingsRequested: settingsDialog.open()
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
                visible: !root.connected && !AppController.connecting
                dataDate: AppController.lastConnectionText
                onConnectRequested: root.connectRequested()
            }

            MessageBar {
                id: messageBar

                objectName: "messageBar"
                Layout.fillWidth: true
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

    ConnectDialog {
        id: connectDialog

        objectName: "connectDialog"
        onQrCodeRequested: {
            connectDialog.close();
            qrDialog.open();
        }
    }

    QrConnectDialog {
        id: qrDialog

        objectName: "qrDialog"
    }

    PinDialog {
        id: pinDialog

        objectName: "pinDialog"
        canRemember: AppController.canRememberPin
        onPinEntered: (pin, remember) => AppController.submitPin(pin, remember)
        onCancelled: AppController.cancelPin()
    }

    SettingsDialog {
        id: settingsDialog

        objectName: "settingsDialog"
    }

    ProfilesDialog {
        id: profilesDialog

        objectName: "profilesDialog"
    }

    function showSection(key: string): void {
        for (let i = 0; i < sectionCatalog.all.length; ++i) {
            if (sectionCatalog.all[i].key === key) {
                root.currentSection = i;
                return;
            }
        }
    }
}
