import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root

    width: 1280
    height: 800
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: qsTr("QPhoneManager")

    Label {
        anchors.centerIn: parent
        text: qsTr("QPhoneManager")
    }
}
