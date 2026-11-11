import QtQuick
import QtTest

import QPhoneManager.Ui

Item {
    id: root

    width: 800
    height: 600

    PinDialog {
        id: dialog

        remainingTries: 9
        wrongPin: true
    }

    SignalSpy {
        id: enteredSpy

        target: dialog
        signalName: "pinEntered"
    }

    SignalSpy {
        id: cancelledSpy

        target: dialog
        signalName: "cancelled"
    }

    TestCase {
        name: "PinDialog"
        when: windowShown

        function init(): void {
            enteredSpy.clear();
            cancelledSpy.clear();
            dialog.wrongPin = true;
            dialog.remainingTries = 9;
            dialog.open();
            tryCompare(dialog, "opened", true);
        }

        function cleanup(): void {
            dialog.close();
            tryCompare(dialog, "visible", false);
        }

        function test_connectStaysOffUntilAPinIsTyped(): void {
            const button = findChild(dialog.footer, "pinConnectButton");
            verify(button);
            verify(!button.enabled);

            keyClick(Qt.Key_4);
            keyClick(Qt.Key_2);
            verify(button.enabled);
            keyClick(Qt.Key_Return);

            compare(enteredSpy.count, 1);
            compare(enteredSpy.signalArguments[0][0], "42");
            compare(enteredSpy.signalArguments[0][1], false);
            tryCompare(dialog, "opened", false);
        }

        function test_rememberChoiceIsPassedOn(): void {
            findChild(dialog.contentItem, "rememberPinBox").checked = true;
            keyClick(Qt.Key_7);
            mouseClick(findChild(dialog.footer, "pinConnectButton"));

            compare(enteredSpy.count, 1);
            compare(enteredSpy.signalArguments[0][1], true);
        }

        function test_wrongPinNamesTheTriesLeft(): void {
            const label = findChild(dialog.contentItem, "wrongPinLabel");
            verify(label.visible);
            verify(label.text.indexOf("9") >= 0);

            dialog.remainingTries = 0;
            verify(label.text.indexOf("stops") >= 0);
        }

        function test_firstAskHidesTheWrongPinNote(): void {
            dialog.wrongPin = false;
            verify(!findChild(dialog.contentItem, "wrongPinLabel").visible);
        }

        function test_escapeCancels(): void {
            keyClick(Qt.Key_Escape);
            tryCompare(cancelledSpy, "count", 1);
            compare(enteredSpy.count, 0);
        }

        function test_fieldIsEmptyWhenOpenedAgain(): void {
            keyClick(Qt.Key_1);
            dialog.close();
            tryCompare(dialog, "visible", false);
            dialog.open();
            tryCompare(dialog, "opened", true);
            const field = findChild(dialog.contentItem, "pinField");
            compare(field.text, "");
            verify(field.activeFocus);
            compare(field.echoMode, TextInput.Password);
        }
    }
}
