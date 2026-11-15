import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property Component listComponent
    property Component detailComponent
    property bool detailOpen: false
    // Part of the width the list takes at first.
    property real listShare: 0.4
    // Not narrow before the first layout, so the views are not built twice.
    readonly property bool narrow: root.width > 0 && root.width < Theme.narrowContentWidth

    SplitView {
        id: split

        objectName: "listDetailSplit"
        anchors.fill: parent
        visible: !root.narrow

        // A thin line over the full height; the style's own handle draws a short
        // grip mark that reads as a stray stroke between list and detail.
        handle: Item {
            id: splitHandle

            implicitWidth: Theme.gridUnit

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 1
                height: parent.height
                color: splitHandle.SplitHandle.hovered || splitHandle.SplitHandle.pressed ? Theme.primary : Theme.outlineVariant
            }
        }

        Loader {
            SplitView.preferredWidth: root.width * root.listShare
            SplitView.minimumWidth: TypeScale.body * 16
            active: !root.narrow
            sourceComponent: root.listComponent
        }

        Loader {
            SplitView.fillWidth: true
            SplitView.minimumWidth: TypeScale.body * 16
            active: !root.narrow
            sourceComponent: root.detailComponent
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: root.narrow
        spacing: 0

        ToolButton {
            id: backButton

            visible: root.detailOpen
            text: qsTr("Back")
            icon.source: Theme.icon("chevron-left")
            onClicked: root.detailOpen = false

            FocusRing {
                shown: backButton.activeFocus
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.narrow
            sourceComponent: root.detailOpen ? root.detailComponent : root.listComponent
        }
    }
}
