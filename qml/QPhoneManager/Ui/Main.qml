pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: root

    readonly property bool connected: AppController.connected
    readonly property string currentKey: sectionCatalog.all[root.currentSection].key
    readonly property bool hasData: (root.currentKey === "contacts" && AppController.contacts.hasPhone) || (root.currentKey === "calls" && AppController.calls.hasPhone) || (root.currentKey === "messages" && AppController.messages.hasPhone) || (root.currentKey === "calendar" && AppController.calendar.hasPhone) || (root.currentKey === "tasks" && AppController.tasks.hasPhone) || (root.currentKey === "notes" && AppController.notes.hasPhone) || (root.currentKey === "apps" && AppController.apps.totalCount > 0)
    property int currentSection: 0
    /// The help is shown in place of the current section.
    property bool helpShown: false

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

        title: root.helpShown ? qsTr("Help") : sectionCatalog.all[root.currentSection].title
        iconName: root.helpShown ? "help" : sectionCatalog.all[root.currentSection].iconName
        connected: root.connected
        canSearch: root.hasData && !root.helpShown
        onSyncRequested: AppController.syncAll()
        onConnectRequested: root.connectRequested()
        onPhonesRequested: profilesDialog.open()
    }

    footer: StatusBar {}

    onConnectRequested: connectDialog.open()
    onCurrentSectionChanged: appHeader.clearSearch()
    Component.onCompleted: {
        // Every tool tip of the window is this one shared popup.
        Theme.calmPopup(appHeader.ToolTip.toolTip);
        AppController.start();
    }

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

    Binding {
        target: AppController.calendar
        property: "searchText"
        value: root.currentKey === "calendar" ? appHeader.searchText : ""
    }

    Connections {
        target: AppController.calendar

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }
    }

    Binding {
        target: AppController.tasks
        property: "searchText"
        value: root.currentKey === "tasks" ? appHeader.searchText : ""
    }

    Connections {
        target: AppController.tasks

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }
    }

    Binding {
        target: AppController.notes
        property: "searchText"
        value: root.currentKey === "notes" ? appHeader.searchText : ""
    }

    Connections {
        target: AppController.notes

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }
    }

    Connections {
        target: AppController.files

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }
    }

    Binding {
        target: AppController.apps
        property: "searchText"
        value: root.currentKey === "apps" ? appHeader.searchText : ""
    }

    Connections {
        target: AppController.apps

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }
    }

    Connections {
        target: AppController.screen

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }
    }

    Connections {
        target: AppController.notifications

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }

        function onInfoMessage(message: string): void {
            messageBar.show(message, false);
        }
    }

    Connections {
        target: AppController.call

        function onErrorOccurred(message: string): void {
            messageBar.show(message, true);
        }
    }

    Connections {
        target: AppController.transfers

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
            // The open connect dialog covers the bar and shows the failure itself.
            if (!connectDialog.showsFailures)
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

        function onNewMessageRequested(): void {
            root.bringToFront();
            root.showSection("messages");
            AppController.messages.composeNew();
        }

        function onNotificationsRequested(): void {
            root.bringToFront();
            root.showSection("notifications");
        }
    }

    Sections {
        id: sectionCatalog
    }

    AppShortcuts {
        sectionCount: sectionCatalog.all.length
        canSearch: root.hasData && !root.helpShown
        canSync: root.connected
        onSectionRequested: index => root.showSectionAt(index)
        onSearchRequested: appHeader.focusSearch()
        onSyncRequested: appHeader.syncRequested()
        onQuitRequested: Qt.quit()
        onSettingsRequested: settingsDialog.open()
        onHelpRequested: root.showHelp(root.helpShown ? HelpController.currentTopic : root.currentKey)
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
            helpShown: root.helpShown
            onSectionSelected: index => root.showSectionAt(index)
            onSettingsRequested: settingsDialog.open()
            onBackupRequested: backupDialog.open()
            onHelpRequested: root.showHelp("getting-started")
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Theme.outlineVariant
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            DisconnectedBanner {
                Layout.fillWidth: true
                visible: !root.connected && !AppController.connecting && !root.helpShown
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
                visible: !root.helpShown
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

            HelpPage {
                id: helpPage

                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.helpShown
                onAboutRequested: aboutDialog.open()
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

    BackupDialog {
        id: backupDialog

        objectName: "backupDialog"
        onHelpRequested: {
            backupDialog.close();
            root.showHelp("backup");
        }
    }

    SettingsDialog {
        id: settingsDialog

        objectName: "settingsDialog"
        onHelpRequested: {
            settingsDialog.close();
            root.showHelp("settings");
        }
    }

    AboutDialog {
        id: aboutDialog

        objectName: "aboutDialog"
    }

    ProfilesDialog {
        id: profilesDialog

        objectName: "profilesDialog"
    }

    FirstSyncPopup {
        objectName: "firstSyncPopup"
        running: AppController.firstSync
        step: AppController.syncStep
        stepCount: AppController.syncStepCount
        areaName: AppController.syncArea
    }

    CallWindow {
        objectName: "callWindow"
    }

    function bringToFront(): void {
        root.show();
        root.raise();
        root.requestActivate();
    }

    function showSection(key: string): void {
        for (let i = 0; i < sectionCatalog.all.length; ++i) {
            if (sectionCatalog.all[i].key === key) {
                root.showSectionAt(i);
                return;
            }
        }
    }

    function showSectionAt(index: int): void {
        root.helpShown = false;
        root.currentSection = index;
    }

    // Opens the chapter of `topic` and moves the focus into it, so that the
    // keys scroll the text right away.
    function showHelp(topic: string): void {
        HelpController.openTopic(topic);
        root.helpShown = true;
        helpPage.focusContent();
    }
}
