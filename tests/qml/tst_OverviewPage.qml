import QtQuick
import QtTest

import QPhoneManager.Ui
import QPhoneManager.TestSupport

Item {
    id: root

    width: 1200
    height: 900

    OverviewPage {
        id: page

        anchors.fill: parent
    }

    SignalSpy {
        id: connectSpy

        target: page
        signalName: "connectRequested"
    }

    TestCase {
        name: "OverviewPage"
        when: windowShown

        function cleanup(): void {
            AppController.disconnectPhone();
            connectSpy.clear();
        }

        function test_withoutPhoneOffersToConnect(): void {
            verify(findChild(page, "overviewEmpty").visible);
            verify(!findChild(page, "overviewLoading").visible);

            mouseClick(findChild(page, "overviewEmptyAction"));

            compare(connectSpy.count, 1);
        }

        function test_showsProgressWhileConnecting(): void {
            verify(AppController.connectAddress(TestPhone.address));

            verify(findChild(page, "overviewLoading").visible);
            verify(!findChild(page, "overviewEmpty").visible);
            tryCompare(AppController, "connected", true, 10000);
        }

        function test_connectedPhoneFillsTheCards(): void {
            verify(AppController.connectAddress(TestPhone.address));
            tryCompare(AppController, "connected", true, 10000);

            tryVerify(() => findChild(page, "displayValue").text === "1080 x 2400 pixels, 420 dpi", 10000);
            compare(findChild(page, "deviceNameValue").text, "Example Phone X1");
            compare(findChild(page, "androidValue").text, "Android 14 (Upside Down Cake)");
            compare(findChild(page, "batteryValue").text, "72 %");
            compare(findChild(page, "signalValue").text, "Good");
            compare(findChild(page, "internalStorageValue").text, "32.0 GB of 64.0 GB free");
            compare(findChild(page, "simEmpty").text, "No SIM card found.");
            verify(!findChild(page, "overviewEmpty").visible);
        }
    }
}
