pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: root

    required property TransferQueue transfers

    padding: Theme.gridUnit * 2
    Material.background: Theme.surfaceContainer

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.gridUnit

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gridUnit * 2

            Label {
                objectName: "transferSummary"
                Layout.fillWidth: true
                text: root.transfers.active ? root.transfers.statusText : qsTr("Transfers finished")
                font.bold: true
                elide: Text.ElideRight
            }

            Button {
                id: cancelAllButton

                objectName: "cancelAllTransfersButton"
                visible: root.transfers.active
                text: qsTr("Cancel all")
                flat: true
                onClicked: root.transfers.cancelAll()

                FocusRing {
                    shown: cancelAllButton.visualFocus
                }
            }

            Button {
                id: clearButton

                objectName: "clearTransfersButton"
                visible: root.transfers.hasFinished
                text: qsTr("Clear the list")
                flat: true
                onClicked: root.transfers.clearFinished()

                FocusRing {
                    shown: clearButton.visualFocus
                }
            }
        }

        ProgressBar {
            objectName: "transferProgress"
            Layout.fillWidth: true
            visible: root.transfers.active
            value: root.transfers.progress
            Accessible.name: root.transfers.statusText
        }

        ListView {
            id: jobList

            objectName: "transferList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(jobList.contentHeight, TypeScale.body * 9)
            // Long transfers list more files than the panel shows.
            clip: true
            model: root.transfers
            activeFocusOnTab: true
            keyNavigationEnabled: true
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Transfers")
            ScrollBar.vertical: ScrollBar {}

            // The row shows the current job by its colour; Delete cancels it.
            delegate: Rectangle {
                id: jobRow

                required property int index
                required property int jobId
                required property string name
                required property int direction
                required property string stateText
                required property bool canCancel

                width: ListView.view.width
                implicitHeight: jobContent.implicitHeight + Theme.gridUnit
                color: ListView.isCurrentItem && jobList.activeFocus ? Theme.surfaceVariant : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: jobRow.name
                Accessible.description: jobRow.stateText

                RowLayout {
                    id: jobContent

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.gridUnit * 2
                    spacing: Theme.gridUnit * 2

                    TintedIcon {
                        Layout.preferredWidth: Theme.listIconSize
                        Layout.preferredHeight: Theme.listIconSize
                        name: jobRow.direction === TransferQueue.Upload ? "upload" : "download"
                        color: Theme.foregroundOnSurfaceVariant
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 3
                        text: jobRow.name
                        elide: Text.ElideMiddle
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 2
                        text: jobRow.stateText
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                        elide: Text.ElideRight
                    }

                    ToolButton {
                        id: cancelJobButton

                        text: qsTr("Cancel the transfer of %1").arg(jobRow.name)
                        display: AbstractButton.IconOnly
                        icon.source: Theme.icon("x")
                        enabled: jobRow.canCancel
                        // The list takes the focus; Delete cancels the job shown there.
                        focusPolicy: Qt.NoFocus
                        Accessible.name: cancelJobButton.text
                        ToolTip.visible: cancelJobButton.hovered
                        ToolTip.text: cancelJobButton.text
                        onClicked: root.transfers.cancel(jobRow.jobId)
                    }
                }
            }

            Keys.onDeletePressed: root.transfers.cancel(root.transfers.idAt(jobList.currentIndex))

            FocusRing {
                shown: jobList.activeFocus
            }
        }
    }
}
