import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    required property string label
    required property string value
    property string valueObjectName: ""

    spacing: Theme.gridUnit * 2

    Label {
        Layout.preferredWidth: TypeScale.body * 9
        text: root.label
        color: Theme.foregroundOnSurfaceVariant
        elide: Text.ElideRight
    }

    Label {
        objectName: root.valueObjectName
        Layout.fillWidth: true
        text: root.value.length > 0 ? root.value : qsTr("Not known")
        wrapMode: Text.Wrap
    }
}
