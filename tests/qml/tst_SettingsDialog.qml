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
