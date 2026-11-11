import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property Component listComponent
    property Component detailComponent
    property bool detailOpen: false
    readonly property bool narrow: root.width < Theme.narrowContentWidth

    SplitView {
        anchors.fill: parent
        visible: !root.narrow

        Loader {
            SplitView.preferredWidth: root.width * 0.4
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
                shown: backButton.visualFocus
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
