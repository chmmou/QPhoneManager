import QtQuick
import QtTest

import QPhoneManager.Ui

Item {
    id: root

    width: 400
    height: 300

    AppShortcuts {
        id: shortcuts

        sectionCount: 3
        canSync: true
    }

    SignalSpy {
        id: quitSpy

        target: shortcuts
        signalName: "quitRequested"
    }

    SignalSpy {
        id: syncSpy

        target: shortcuts
        signalName: "syncRequested"
    }

    TestCase {
        name: "AppShortcuts"
        when: windowShown

        function init(): void {
            shortcuts.canSync = true;
            quitSpy.clear();
            syncSpy.clear();
        }

        // Under the offscreen theme StandardKey.Quit resolved to no key at all.
        function test_ctrlQQuitsOnEveryTheme(): void {
            keyClick(Qt.Key_Q, Qt.ControlModifier);

            compare(quitSpy.count, 1);
            compare(syncSpy.count, 0);
        }

        // Guards against a return to StandardKey.Refresh, which gives F5 on some
        // themes only; the offscreen theme used here does give F5.
        function test_f5SyncsOnEveryTheme(): void {
            keyClick(Qt.Key_F5);

            compare(syncSpy.count, 1);
            compare(quitSpy.count, 0);
        }

        function test_f5DoesNothingWithoutAPhone(): void {
            shortcuts.canSync = false;

            keyClick(Qt.Key_F5);

            compare(syncSpy.count, 0);
        }
    }
}
