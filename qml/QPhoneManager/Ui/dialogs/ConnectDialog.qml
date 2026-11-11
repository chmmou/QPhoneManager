import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    property bool searchOnOpen: true

    signal qrCodeRequested

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 36, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 36)
    height: Math.min(TypeScale.body * 34, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 34)
    modal: true
    Overlay.modal: Scrim {}
    // The raised look of Material draws its dialog through a shader effect,
    // which the software renderer leaves out together with the background.
    Material.elevation: 0
    title: qsTr("Connect a phone")

    footer: DialogButtonBox {
        Button {
            id: closeButton

            objectName: "connectCloseButton"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: closeButton.visualFocus
            }
        }
    }

    onOpened: {
        if (root.searchOnOpen)
            AppController.startSearch();
        addressField.forceActiveFocus(Qt.PopupFocusReason);
    }
    onClosed: AppController.stopSearch()

    Connections {
        target: AppController

        function onConnectionStateChanged(): void {
            if (AppController.connected && root.opened)
                root.close();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        TabBar {
            id: tabs

            objectName: "connectTabs"
            Layout.fillWidth: true

            TabButton {
                id: wifiTab

                text: qsTr("Wi-Fi")
                icon.source: Theme.icon("wifi")

                FocusRing {
                    shown: wifiTab.visualFocus
                }
            }

            TabButton {
                id: usbTab

                text: qsTr("USB")
                icon.source: Theme.icon("usb")

                FocusRing {
                    shown: usbTab.visualFocus
                }
            }

            TabButton {
                id: bluetoothTab

                text: qsTr("Bluetooth")
                icon.source: Theme.icon("bluetooth")

                FocusRing {
                    shown: bluetoothTab.visualFocus
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: AppController.connecting
            spacing: Theme.gridUnit * 2

            BusyIndicator {
                Layout.preferredWidth: TypeScale.body * 2
                Layout.preferredHeight: TypeScale.body * 2
                running: AppController.connecting
                Accessible.name: qsTr("Connecting")
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Connecting to the phone.")
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: Theme.gridUnit * 3

                Label {
                    objectName: "wifiNotice"
                    Layout.fillWidth: true
                    text: qsTr("Wi-Fi connections are not encrypted. Use them only in a network you trust, or connect over USB.")
                    font.pixelSize: TypeScale.hint
                    color: Theme.foregroundOnSurfaceVariant
                    wrapMode: Text.Wrap
                }

                Label {
                    text: qsTr("Phones in this network")
                    font.pixelSize: TypeScale.section
                }

                FoundPhoneList {
                    Layout.fillWidth: true
                    phones: AppController.wifiPhones
                    listName: qsTr("Phones in this network")
                    emptyText: AppController.wifiHint.length > 0 ? AppController.wifiHint : qsTr("No phone found yet. Open MyPhoneExplorer Client on the phone. The phone needs to be in the same network.")
                    onChosen: address => AppController.connectAddress(address)
                }

                Label {
                    text: qsTr("Address of the phone")
                    font.pixelSize: TypeScale.section
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    TextField {
                        id: addressField

                        objectName: "addressField"
                        Layout.fillWidth: true
                        placeholderText: qsTr("For example 192.168.1.20")
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        Accessible.name: qsTr("Address of the phone")
                        onAccepted: {
                            if (addressField.text.length > 0)
                                AppController.connectAddress(addressField.text);
                        }
                    }

                    Button {
                        id: addressButton

                        objectName: "addressConnectButton"
                        text: qsTr("Connect")
                        enabled: addressField.text.trim().length > 0 && !AppController.connecting
                        onClicked: AppController.connectAddress(addressField.text)

                        FocusRing {
                            shown: addressButton.visualFocus
                        }
                    }
                }

                Button {
                    id: qrButton

                    objectName: "qrButton"
                    text: qsTr("Show QR code")
                    icon.source: Theme.icon("qrcode")
                    onClicked: root.qrCodeRequested()

                    FocusRing {
                        shown: qrButton.visualFocus
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                spacing: Theme.gridUnit * 3

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Connect the phone with a USB cable and turn on USB debugging. QPhoneManager then starts MyPhoneExplorer Client on the phone.")
                    wrapMode: Text.Wrap
                }

                FoundPhoneList {
                    Layout.fillWidth: true
                    phones: AppController.usbPhones
                    listName: qsTr("Phones at USB")
                    emptyText: AppController.usbHint.length > 0 ? AppController.usbHint : (AppController.searching ? qsTr("Looking for phones at USB.") : qsTr("No phone found at USB."))
                    onChosen: address => AppController.connectUsb(address)
                }

                Button {
                    id: usbSearchButton

                    text: qsTr("Search again")
                    icon.source: Theme.icon("refresh")
                    enabled: !AppController.searching
                    onClicked: AppController.startSearch()

                    FocusRing {
                        shown: usbSearchButton.visualFocus
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                spacing: Theme.gridUnit * 3

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Pair the phone with this computer in the system settings first. Paired phones in reach appear here.")
                    wrapMode: Text.Wrap
                }

                FoundPhoneList {
                    Layout.fillWidth: true
                    phones: AppController.bluetoothPhones
                    listName: qsTr("Paired phones")
                    emptyText: AppController.bluetoothHint.length > 0 ? AppController.bluetoothHint : (AppController.searching ? qsTr("Looking for paired phones.") : qsTr("No paired phone found."))
                    onChosen: address => AppController.connectBluetooth(address)
                }

                Button {
                    id: bluetoothSearchButton

                    text: qsTr("Search again")
                    icon.source: Theme.icon("refresh")
                    enabled: !AppController.searching
                    onClicked: AppController.startSearch()

                    FocusRing {
                        shown: bluetoothSearchButton.visualFocus
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }
}
