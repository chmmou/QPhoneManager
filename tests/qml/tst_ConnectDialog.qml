import QtQuick
import QtTest

import QPhoneManager.Ui
import QPhoneManager.TestSupport

Item {
    id: root

    width: 1000
    height: 800

    ConnectDialog {
        id: dialog

        searchOnOpen: false
    }

    SignalSpy {
        id: qrSpy

        target: dialog
        signalName: "qrCodeRequested"
    }

    TestCase {
        name: "ConnectDialog"
        when: windowShown

        function init(): void {
            qrSpy.clear();
            dialog.open();
            tryCompare(dialog, "opened", true);
        }

        function cleanup(): void {
            AppController.disconnectPhone();
            findChild(dialog.contentItem, "addressField").text = "";
            dialog.close();
            tryCompare(dialog, "visible", false);
        }

        function test_offersWifiUsbAndBluetooth(): void {
            const tabs = findChild(dialog.contentItem, "connectTabs");
            compare(tabs.count, 3);
            compare(tabs.currentIndex, 0);
        }

        function test_wifiPageSaysTheLinkIsNotEncrypted(): void {
            const notice = findChild(dialog.contentItem, "wifiNotice");
            verify(notice.visible);
            verify(notice.text.indexOf("not encrypted") >= 0);
        }

        function test_connectNeedsAnAddress(): void {
            const button = findChild(dialog.contentItem, "addressConnectButton");
            verify(!button.enabled);
            findChild(dialog.contentItem, "addressField").text = "   ";
            verify(!button.enabled);
            findChild(dialog.contentItem, "addressField").text = TestPhone.address;
            verify(button.enabled);
        }

        function test_typedAddressConnectsAndClosesTheDialog(): void {
            findChild(dialog.contentItem, "addressField").text = TestPhone.address;
            mouseClick(findChild(dialog.contentItem, "addressConnectButton"));

            tryCompare(AppController, "connected", true, 10000);
            tryCompare(dialog, "opened", false);
        }

        function test_qrButtonAsksForTheQrCode(): void {
            mouseClick(findChild(dialog.contentItem, "qrButton"));
            compare(qrSpy.count, 1);
        }
    }
}
