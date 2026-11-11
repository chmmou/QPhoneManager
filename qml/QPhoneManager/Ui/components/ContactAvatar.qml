import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property string photoUrl
    required property string initials
    property int size: 40

    implicitWidth: root.size
    implicitHeight: root.size
    radius: Theme.gridUnit * 2
    color: Theme.surfaceVariant
    Accessible.ignored: true

    Label {
        anchors.centerIn: parent
        visible: photo.status !== Image.Ready
        text: root.initials
        font.pixelSize: Math.round(root.size * 0.4)
        color: Theme.foregroundOnSurfaceVariant
    }

    Image {
        id: photo

        anchors.fill: parent
        visible: photo.status === Image.Ready
        source: root.photoUrl
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
}
