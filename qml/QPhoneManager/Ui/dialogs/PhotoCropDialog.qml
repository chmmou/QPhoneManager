pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    required property ContactEditor editor
    readonly property real pictureScale: picture.paintedWidth > 0 ? picture.paintedWidth / Math.max(1, root.editor.candidateWidth) : 1
    readonly property real offsetX: (picture.width - picture.paintedWidth) / 2
    readonly property real offsetY: (picture.height - picture.paintedHeight) / 2
    readonly property real maxSide: Math.min(picture.paintedWidth, picture.paintedHeight)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 36, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 36)
    height: Math.min(TypeScale.body * 38, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 38)
    modal: true
    Overlay.modal: Scrim {}
    Material.elevation: 0
    title: qsTr("Choose the part of the picture")

    footer: DialogButtonBox {
        DialogButton {
            id: applyButton

            objectName: "applyCropButton"
            text: qsTr("Use this part")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }

        DialogButton {
            id: cancelButton

            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    onOpened: {
        selection.side = root.maxSide;
        selection.x = root.offsetX + (picture.paintedWidth - selection.side) / 2;
        selection.y = root.offsetY + (picture.paintedHeight - selection.side) / 2;
        frame.forceActiveFocus(Qt.PopupFocusReason);
    }
    onAccepted: {
        root.editor.applyCrop(Math.round((selection.x - root.offsetX) / root.pictureScale), Math.round((selection.y - root.offsetY) / root.pictureScale), Math.round(selection.side / root.pictureScale));
    }
    onRejected: root.editor.dropCandidate()
    Component.onCompleted: Theme.calmPopup(root)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: qsTr("Drag the frame or move it with the arrow keys. Plus and minus change its size.")
            wrapMode: Text.Wrap
        }

        Item {
            id: frame

            objectName: "cropArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            activeFocusOnTab: true
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Part of the picture for the photo")

            Image {
                id: picture

                anchors.fill: parent
                source: root.editor.candidateUrl
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 1024
                sourceSize.height: 1024
                Accessible.ignored: true
            }

            Rectangle {
                id: selection

                property real side: 0

                width: selection.side
                height: selection.side
                color: "transparent"
                border.width: 3
                border.color: Theme.primary

                DragHandler {
                    xAxis.minimum: root.offsetX
                    xAxis.maximum: root.offsetX + picture.paintedWidth - selection.side
                    yAxis.minimum: root.offsetY
                    yAxis.maximum: root.offsetY + picture.paintedHeight - selection.side
                }
            }

            FocusRing {
                shown: frame.activeFocus
            }

            Keys.onPressed: event => {
                const step = Theme.gridUnit * 2;
                if (event.key === Qt.Key_Left)
                    root.moveSelection(-step, 0);
                else if (event.key === Qt.Key_Right)
                    root.moveSelection(step, 0);
                else if (event.key === Qt.Key_Up)
                    root.moveSelection(0, -step);
                else if (event.key === Qt.Key_Down)
                    root.moveSelection(0, step);
                else if (event.key === Qt.Key_Plus)
                    root.resizeSelection(step * 2);
                else if (event.key === Qt.Key_Minus)
                    root.resizeSelection(-step * 2);
                else
                    return;
                event.accepted = true;
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gridUnit * 3

            Label {
                text: qsTr("Size")
            }

            Slider {
                id: sizeSlider

                Layout.fillWidth: true
                from: Math.min(64 * root.pictureScale, root.maxSide)
                to: root.maxSide
                value: selection.side
                Accessible.name: qsTr("Size of the part")
                onMoved: root.resizeSelection(sizeSlider.value - selection.side)

                FocusRing {
                    shown: sizeSlider.visualFocus
                }
            }
        }
    }

    function moveSelection(dx: real, dy: real): void {
        selection.x = Math.max(root.offsetX, Math.min(root.offsetX + picture.paintedWidth - selection.side, selection.x + dx));
        selection.y = Math.max(root.offsetY, Math.min(root.offsetY + picture.paintedHeight - selection.side, selection.y + dy));
    }

    function resizeSelection(delta: real): void {
        const side = Math.max(sizeSlider.from, Math.min(root.maxSide, selection.side + delta));
        const centerX = selection.x + selection.side / 2;
        const centerY = selection.y + selection.side / 2;
        selection.side = side;
        selection.x = centerX - side / 2;
        selection.y = centerY - side / 2;
        root.moveSelection(0, 0);
    }
}
