import QtQuick
import QtTest

import QPhoneManager.Ui

Item {
    id: root

    width: 800
    height: 600

    FirstSyncPopup {
        id: popup

        stepCount: 5
    }

    TestCase {
        name: "FirstSyncPopup"
        when: windowShown

        function init(): void {
            popup.step = 1;
            popup.areaName = "Contacts";
            popup.running = true;
            tryCompare(popup, "opened", true);
        }

        function cleanup(): void {
            popup.running = false;
            tryCompare(popup, "visible", false);
        }

        function test_showsTheAreaAndTheStep(): void {
            const area = findChild(popup.contentItem, "firstSyncArea");
            verify(area);
            compare(area.text, "Contacts (1 of 5)");
            const bar = findChild(popup.contentItem, "firstSyncProgress");
            compare(bar.value, 0);
            compare(bar.to, 5);

            popup.step = 4;
            popup.areaName = "Calendar and tasks";
            compare(area.text, "Calendar and tasks (4 of 5)");
            compare(bar.value, 3);
        }

        function test_escapeDoesNotClose(): void {
            keyClick(Qt.Key_Escape);
            verify(popup.opened);
            verify(popup.visible);
        }

        function test_clickOutsideDoesNotClose(): void {
            mouseClick(root, 5, 5);
            verify(popup.opened);
            verify(popup.visible);
        }

        function test_offersNoButtonToClose(): void {
            verify(!popup.footer || !popup.footer.visible || popup.footer.contentChildren.length === 0);
        }

        function test_closesWhenTheSyncEnds(): void {
            popup.running = false;
            tryCompare(popup, "visible", false);
        }
    }
}
