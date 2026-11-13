import QtQuick
import QtQuick.Controls
import QtTest

import QPhoneManager.Ui

Item {
    id: root

    width: 800
    height: 600

    ConfirmDeleteDialog {
        id: dialog

        title: "Delete?"
        message: "Gone for good."
    }

    TextField {
        id: field

        y: 300
        width: 200
    }

    TabBar {
        id: tabs

        Component.onCompleted: Theme.calmTabBar(tabs)

        TabButton {
            text: "One"
        }

        TabButton {
            text: "Two"
        }
    }

    TestCase {
        name: "ReducedMotion"
        when: windowShown

        function cleanup(): void {
            dialog.close();
            tryCompare(dialog, "visible", false);
            Settings.reduceMotion = false;
        }

        // The setting was stored and used nowhere; dialogs still zoomed in.
        function test_dialogOpensWithoutMotion(): void {
            Settings.reduceMotion = true;
            compare(dialog.enter, null);
            compare(dialog.exit, null);

            dialog.open();

            verify(dialog.opened);
        }

        function test_dialogKeepsItsMotionByDefault(): void {
            verify(dialog.enter !== null);
            verify(dialog.exit !== null);

            dialog.open();

            verify(!dialog.opened);
            tryCompare(dialog, "opened", true);
        }

        function test_motionComesBackWhenTheSettingIsOff(): void {
            Settings.reduceMotion = true;
            dialog.open();
            verify(dialog.opened);
            dialog.close();
            tryCompare(dialog, "visible", false);

            Settings.reduceMotion = false;
            verify(dialog.enter !== null);
            verify(dialog.exit !== null);
            dialog.open();

            verify(!dialog.opened);
            tryCompare(dialog, "opened", true);
        }

        // Qt 6.11 and later give every text field a menu of their own.
        function test_textMenuOpensWithoutMotion(): void {
            const menu = field.ContextMenu.menu;
            if (!menu)
                skip("This Qt gives text fields no menu of their own");
            Settings.reduceMotion = true;

            mouseClick(field, 20, field.height / 2, Qt.RightButton);

            compare(menu.enter, null);
            verify(menu.opened);
            menu.close();
            tryCompare(menu, "visible", false);
            Settings.reduceMotion = false;
            mouseClick(field, 20, field.height / 2, Qt.RightButton);
            verify(menu.enter !== null);
            verify(!menu.opened);
            menu.close();
            tryCompare(menu, "visible", false);
        }

        function test_tabMarkerJumpsWithoutMotion(): void {
            const list = tabs.contentItem as ListView;
            const usual = list.highlightMoveDuration;
            verify(usual > 0);

            Settings.reduceMotion = true;
            compare(list.highlightMoveDuration, 0);

            Settings.reduceMotion = false;
            compare(list.highlightMoveDuration, usual);
        }
    }
}
