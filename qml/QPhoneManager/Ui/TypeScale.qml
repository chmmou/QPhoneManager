pragma Singleton

import QtQuick

QtObject {
    id: root

    readonly property int base: 16
    readonly property real ratio: 1.2
    readonly property int title: root.step(2)
    readonly property int section: root.step(1)
    readonly property int body: root.base
    readonly property int hint: root.step(-1)

    function step(n: int): int {
        return Math.round(root.base * Math.pow(root.ratio, n));
    }
}
