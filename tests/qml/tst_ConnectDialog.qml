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
            TestPhone.setRejectAsAlreadyConnected(false);
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

        function failAnAttempt(): void {
            TestPhone.setRejectAsAlreadyConnected(true);
            findChild(dialog.contentItem, "addressField").text = TestPhone.address;
            mouseClick(findChild(dialog.contentItem, "addressConnectButton"));
            tryCompare(findChild(dialog.contentItem, "connectFailure"), "visible", true, 10000);
        }

        // The failure went only to the message bar of the window, which the
        // open dialog covers, and closed there unread.
        function test_failedAttemptShowsItsMessageInTheDialog(): void {
            failAnAttempt();

            const text = findChild(dialog.contentItem, "connectFailureText");
            verify(text.text.indexOf("another computer") >= 0);
            verify(dialog.opened);
            verify(dialog.showsFailures);
        }

        function test_nextAttemptClearsTheFailure(): void {
            failAnAttempt();
            TestPhone.setRejectAsAlreadyConnected(false);

            mouseClick(findChild(dialog.contentItem, "addressConnectButton"));

            verify(AppController.connecting);
            compare(dialog.failureText, "");
            verify(!findChild(dialog.contentItem, "connectFailure").visible);
            tryCompare(AppController, "connected", true, 10000);
        }

        function test_closingTheDialogClearsTheFailure(): void {
            failAnAttempt();

            dialog.close();
            tryCompare(dialog, "visible", false);
            compare(dialog.failureText, "");
            verify(!dialog.showsFailures);

            dialog.open();
            tryCompare(dialog, "opened", true);
            verify(!findChild(dialog.contentItem, "connectFailure").visible);
        }

        function test_longFailureWrapsAndLeavesTheButtonsInTheDialog(): void {
            dialog.failureText = "This computer cannot reach MyPhoneExplorer Client on the phone over Bluetooth. Keep the phone close with Bluetooth on, allow the app the permission \"Nearby devices\" and turn on Bluetooth in the app. If the app started while the phone was already connected, disconnect the phone once in the Bluetooth settings of this computer.";

            const text = findChild(dialog.contentItem, "connectFailureText");
            tryVerify(() => text.lineCount > 1);
            const qrButton = findChild(dialog.contentItem, "qrButton");
            tryVerify(() => qrButton.mapToItem(dialog.contentItem, 0, qrButton.height).y <= dialog.contentItem.height);
        }
    }
}
