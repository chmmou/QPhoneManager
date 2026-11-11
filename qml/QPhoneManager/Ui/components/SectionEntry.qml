import QtQuick

QtObject {
    id: root

    required property string key
    required property string title
    required property string iconName
    required property string emptyText
    property string emptyTitle: ""
    property bool hasList: true
}
