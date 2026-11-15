import QtQuick
import QtTest

import QPhoneManager.Ui

Item {
    id: root

    width: 1000
    height: 800

    Component {
        id: dialogComponent

        SettingsDialog {}
    }

    TestCase {
        id: testCase

        property SettingsDialog dialog: null

        name: "SettingsDialog"
        when: windowShown

        function cleanup(): void {
            if (testCase.dialog) {
                testCase.dialog.close();
                testCase.dialog.destroy();
                testCase.dialog = null;
            }
            Settings.language = "";
            Settings.adbPath = "";
        }

        function languageBox(): Item {
            return findChild(testCase.dialog.contentItem, "languageBox");
        }

        // The box was evaluated before its model was filled and stayed empty.
        function test_systemLanguageShowsItsEntry(): void {
            Settings.language = "";
            testCase.dialog = dialogComponent.createObject(root);

            compare(languageBox().currentIndex, 0);
            compare(languageBox().displayText, "Like the system");
        }

        function test_chosenLanguageShowsItsEntry(): void {
            Settings.language = "de";
            testCase.dialog = dialogComponent.createObject(root);

            compare(languageBox().currentValue, "de");
            compare(languageBox().displayText, "Deutsch");
        }

        // The hint that adb is missing stayed while a working adb was set. A path
        // that leads nowhere while adb lies in a usual place is not a missing adb.
        function test_adbHintFollowsThePath(): void {
            Settings.adbPath = "/bin/sh";
            testCase.dialog = dialogComponent.createObject(root);
            testCase.dialog.open();
            tryCompare(testCase.dialog, "opened", true);
            const hint = findChild(testCase.dialog.contentItem, "adbHint");
            const box = findChild(testCase.dialog.contentItem, "adbPathBox");
            verify(hint);
            verify(box);
            compare(box.editText, "/bin/sh");
            compare(hint.text, "");

            box.model = ["/opt/android-sdk/platform-tools/adb"];
            box.forceActiveFocus();
            tryVerify(() => box.activeFocus);
            box.editText = "/nonexistent/adb";
            // Nothing changes while the path is still being typed.
            compare(hint.text, "");
            // Leaving the field ends the editing.
            const next = findChild(testCase.dialog.contentItem, "autoConnectBox");
            next.forceActiveFocus();
            tryVerify(() => !box.activeFocus);
            compare(Settings.adbPath, "/nonexistent/adb");
            compare(hint.text, "No usable adb at this path. Choose adb, or leave the field empty to use /opt/android-sdk/platform-tools/adb.");

            box.model = [];
            verify(hint.text.indexOf("adb was not found on this computer") === 0);

            box.forceActiveFocus();
            tryVerify(() => box.activeFocus);
            box.editText = "/bin/sh";
            next.forceActiveFocus();
            tryVerify(() => !box.activeFocus);
            compare(Settings.adbPath, "/bin/sh");
            compare(hint.text, "");
        }

        function test_choosingAnEntryStoresIt(): void {
            Settings.language = "";
            testCase.dialog = dialogComponent.createObject(root);
            findChild(testCase.dialog.contentItem, "settingsTabs").currentIndex = 2;
            testCase.dialog.open();
            tryCompare(testCase.dialog, "opened", true);
            const box = languageBox();
            box.forceActiveFocus();
            tryVerify(() => box.activeFocus);

            keyClick(Qt.Key_Down);

            compare(Settings.language, "de");
            compare(box.displayText, "Deutsch");
        }
    }
}
