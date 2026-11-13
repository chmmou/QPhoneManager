pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: root

    readonly property BackupController backup: AppController.backup
    property url openedFile: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 40, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 40)
    height: Math.min(TypeScale.body * 42, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 42)
    modal: true
    Overlay.modal: Scrim {}
    // The raised look of Material draws its dialog through a shader effect,
    // which the software renderer leaves out together with the background.
    Material.elevation: 0
    title: qsTr("Back up and restore")

    footer: DialogButtonBox {
        Button {
            id: closeButton

            objectName: "backupCloseButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: closeButton.visualFocus
            }
        }
    }

    // The tab reason makes the tab show its focus ring right away; giving the
    // focus up on closing makes the next opening set that reason again.
    onOpened: {
        root.backup.reset();
        (tabs.currentIndex === 0 ? backupTab : restoreTab).forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        backupTab.focus = false;
        restoreTab.focus = false;
    }

    // The shortcut of the window does not reach past a modal dialog, so F1
    // asks for the chapter here. Off while closed, so the two never compete.
    Shortcut {
        sequences: [StandardKey.HelpContents]
        enabled: root.opened
        onActivated: root.helpRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        TabBar {
            id: tabs

            objectName: "backupTabs"
            Layout.fillWidth: true
            enabled: !root.backup.busy

            TabButton {
                id: backupTab

                objectName: "backupTab"
                width: implicitWidth
                text: qsTr("Back up")

                FocusRing {
                    shown: backupTab.visualFocus
                }
            }

            TabButton {
                id: restoreTab

                objectName: "restoreTab"
                width: implicitWidth
                text: qsTr("Restore")

                FocusRing {
                    shown: restoreTab.visualFocus
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                id: backupPage

                objectName: "backupPage"
                spacing: Theme.gridUnit * 2

                // Scrolls only where the window is too low; the button below stays in view.
                ScrollView {
                    id: backupScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: backupScroll.availableWidth

                    ColumnLayout {
                        width: backupScroll.availableWidth
                        spacing: Theme.gridUnit * 2

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Choose what goes into the backup. It holds what this computer stored at the last sync.")
                            wrapMode: Text.Wrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.gridUnit * 4
                            rowSpacing: 0

                            Repeater {
                                model: root.backup.sections

                                delegate: SectionChoice {
                                    listName: "backup"
                                    sectionModel: root.backup.sections
                                }
                            }
                        }

                        Label {
                            objectName: "unencryptedHint"
                            Layout.fillWidth: true
                            text: qsTr("The backup file is not encrypted. It holds personal data such as contacts and messages, so keep it in a safe place.")
                            color: Theme.warning
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Button {
                    id: saveButton

                    objectName: "saveBackupButton"
                    text: qsTr("Save backup")
                    icon.source: Theme.icon("device-floppy")
                    enabled: root.backup.hasPhone && !root.backup.busy && root.backup.sections.chosenKeys.length > 0
                    onClicked: {
                        saveDialog.selectedFile = root.backup.suggestedFileUrl(saveDialog.currentFolder);
                        saveDialog.open();
                    }

                    FocusRing {
                        shown: saveButton.visualFocus
                    }
                }
            }

            ColumnLayout {
                id: restorePage

                objectName: "restorePage"
                spacing: Theme.gridUnit * 2

                ScrollView {
                    id: restoreScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: restoreScroll.availableWidth

                    ColumnLayout {
                        width: restoreScroll.availableWidth
                        spacing: Theme.gridUnit * 2

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Open a backup file. QPhoneManager compares it with the phone and shows what a restore would change.")
                            wrapMode: Text.Wrap
                        }

                        Button {
                            id: openButton

                            objectName: "openBackupButton"
                            text: qsTr("Open backup")
                            icon.source: Theme.icon("folder")
                            enabled: root.backup.hasPhone && !root.backup.busy
                            onClicked: openDialog.open()

                            FocusRing {
                                shown: openButton.visualFocus
                            }
                        }

                        // The text holds the phone name from the file; it is shown, never interpreted.
                        Label {
                            objectName: "archiveText"
                            Layout.fillWidth: true
                            visible: root.backup.hasPreview
                            text: root.backup.archiveText
                            textFormat: Text.PlainText
                            font.pixelSize: TypeScale.section
                            wrapMode: Text.Wrap
                        }

                        Label {
                            objectName: "otherPhoneHint"
                            Layout.fillWidth: true
                            visible: root.backup.hasPreview && root.backup.fromOtherPhone
                            text: qsTr("This backup is of another phone. A restore only adds what this phone lacks and changes nothing it has.")
                            color: Theme.warning
                            wrapMode: Text.Wrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            visible: root.backup.hasPreview
                            columns: 2
                            columnSpacing: Theme.gridUnit * 4
                            rowSpacing: 0

                            Repeater {
                                model: root.backup.restoreSections

                                delegate: SectionChoice {
                                    listName: "restore"
                                    sectionModel: root.backup.restoreSections
                                }
                            }
                        }
                    }
                }

                Button {
                    id: restoreButton

                    objectName: "restoreBackupButton"
                    text: qsTr("Restore")
                    icon.source: Theme.icon("arrow-back-up")
                    enabled: root.backup.hasPreview && !root.backup.busy && root.backup.restoreSections.chosenKeys.length > 0
                    onClicked: root.backup.restore(root.openedFile, root.backup.restoreSections.chosenKeys)

                    FocusRing {
                        shown: restoreButton.visualFocus
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.backup.busy
            spacing: Theme.gridUnit * 2

            ProgressBar {
                objectName: "backupProgress"
                Layout.fillWidth: true
                indeterminate: root.backup.progress < 0
                value: Math.max(0, root.backup.progress)
                Accessible.name: root.backup.statusText
            }

            Button {
                id: stopButton

                objectName: "stopBackupButton"
                text: qsTr("Stop")
                flat: true
                onClicked: root.backup.cancel()

                FocusRing {
                    shown: stopButton.visualFocus
                }
            }
        }

        Label {
            id: statusLabel

            objectName: "backupStatus"
            Layout.fillWidth: true
            visible: statusLabel.text.length > 0
            text: root.backup.statusText
            textFormat: Text.PlainText
            color: root.backup.statusIsError ? Theme.error : Theme.foregroundOnSurface
            wrapMode: Text.Wrap
            Accessible.role: root.backup.statusIsError ? Accessible.AlertMessage : Accessible.StaticText
            onTextChanged: {
                if (statusLabel.text.length > 0)
                    statusLabel.Accessible.announce(statusLabel.text);
            }
        }
    }

    FileDialog {
        id: saveDialog

        objectName: "saveBackupDialog"
        title: qsTr("Save backup")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("QPhoneManager backup (*.qpmbackup)")]
        defaultSuffix: "qpmbackup"
        onAccepted: root.backup.create(saveDialog.selectedFile, root.backup.sections.chosenKeys)
    }

    FileDialog {
        id: openDialog

        objectName: "openBackupDialog"
        title: qsTr("Open backup")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("QPhoneManager backup (*.qpmbackup)"), qsTr("All files (*)")]
        onAccepted: {
            root.openedFile = openDialog.selectedFile;
            root.backup.preview(openDialog.selectedFile);
        }
    }

    // One section to choose, with what it holds or what restoring it would do below.
    component SectionChoice: ColumnLayout {
        id: choice

        required property int index
        required property string key
        required property string title
        required property bool chosen
        required property bool selectable
        required property string detail
        required property string listName
        required property BackupSectionModel sectionModel

        // Both columns of the grid get the same share of the width.
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.alignment: Qt.AlignTop
        spacing: 0

        CheckBox {
            id: box

            objectName: choice.listName + "-" + choice.key
            text: choice.title
            checked: choice.chosen
            enabled: choice.selectable && !AppController.backup.busy
            Accessible.description: choice.detail
            onToggled: choice.sectionModel.setChosen(choice.index, box.checked)

            FocusRing {
                shown: box.visualFocus
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: box.leftPadding + box.indicator.width + box.spacing
            visible: choice.detail.length > 0
            text: choice.detail
            textFormat: Text.PlainText
            font.pixelSize: TypeScale.hint
            color: Theme.foregroundOnSurfaceVariant
            wrapMode: Text.Wrap
        }
    }
}
