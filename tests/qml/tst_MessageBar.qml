import QtQuick
import QtTest

import QPhoneManager.Ui

Item {
    id: root

    width: 800
    height: 600

    MessageBar {
        id: bar

        width: root.width
    }

    Component {
        id: barComponent

        MessageBar {}
    }

    Item {
        id: elsewhere

        y: 400
        width: 100
        height: 100
        activeFocusOnTab: true
    }

    TestCase {
        name: "MessageBar"
        when: windowShown

        function init(): void {
            mouseMove(root, root.width - 1, root.height - 1);
            elsewhere.forceActiveFocus();
            bar.autoCloseInterval = 15000;
        }

        function cleanup(): void {
            bar.text = "";
        }

        function test_messageClosesOnItsOwn(): void {
            bar.autoCloseInterval = 50;
            bar.show("Contacts: 1 new, 0 changed, 0 deleted", false);
            verify(bar.visible);
            verify(bar.closing);
            tryCompare(bar, "text", "");
            verify(!bar.visible);
        }

        function test_errorClosesOnItsOwn(): void {
            bar.autoCloseInterval = 50;
            bar.show("The phone does not answer.", true);
            tryCompare(bar, "visible", false);
        }

        function test_defaultWaitIsFifteenSeconds(): void {
            const fresh = createTemporaryObject(barComponent, root);
            verify(fresh);
            compare(fresh.autoCloseInterval, 15000);
        }

        function test_pointerOnTheBarHoldsTheMessage(): void {
            bar.show("Contacts: 1 new, 0 changed, 0 deleted", false);
            mouseMove(bar, bar.width / 2, bar.height / 2);
            tryCompare(bar, "closing", false);

            bar.autoCloseInterval = 50;
            mouseMove(root, root.width - 1, root.height - 1);
            tryCompare(bar, "closing", true);
            tryCompare(bar, "text", "");
        }

        function test_focusInTheBarHoldsTheMessage(): void {
            bar.show("The phone does not answer.", true);
            const close = findChild(bar, "messageCloseButton");
            verify(close);
            close.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(bar, "closing", false);

            bar.autoCloseInterval = 50;
            elsewhere.forceActiveFocus();
            tryCompare(bar, "closing", true);
            tryCompare(bar, "text", "");
        }

        function test_newMessageStartsTheWaitAgain(): void {
            bar.show("First", false);
            bar.autoCloseInterval = 50;
            bar.show("Second", true);
            compare(bar.text, "Second");
            verify(bar.isError);
            tryCompare(bar, "text", "");
        }

        function test_closeButtonStillCloses(): void {
            bar.show("The phone does not answer.", true);
            mouseClick(findChild(bar, "messageCloseButton"));
            compare(bar.text, "");
        }
    }
}
