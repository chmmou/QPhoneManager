pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    required property CategoryModel categories
    required property list<string> colorChoices
    property bool canEdit: false

    signal addRequested(string name, int colorIndex)
    signal editRequested(int row, string name, int colorIndex)
    signal removeRequested(int row)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 32, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 32)
    height: Math.min(TypeScale.body * 34, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 34)
    modal: true
    Overlay.modal: Scrim {}
    Material.elevation: 0

    footer: DialogButtonBox {
        Button {
            id: closeButton

            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: closeButton.visualFocus
            }
        }
    }

    onOpened: {
        nameField.text = "";
        categoryList.currentIndex = -1;
        nameField.forceActiveFocus(Qt.TabFocusReason);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            visible: !root.canEdit
            text: qsTr("Connect the phone to change categories.")
            color: Theme.warning
            wrapMode: Text.Wrap
        }

        ListView {
            id: categoryList

            objectName: "categoryList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The list scrolls through more categories than fit.
            clip: true
            model: root.categories
            currentIndex: -1
            activeFocusOnTab: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Categories")

            delegate: Rectangle {
                id: categoryRow

                required property int index
                required property string name
                required property color categoryColor

                width: ListView.view.width
                implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 3
                color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: categoryRow.name

                TapHandler {
                    onTapped: categoryList.currentIndex = categoryRow.index
                }

                RowLayout {
                    id: rowContent

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.gridUnit * 3
                    anchors.rightMargin: Theme.gridUnit * 3
                    spacing: Theme.gridUnit * 3

                    Rectangle {
                        Layout.preferredWidth: TypeScale.body
                        Layout.preferredHeight: TypeScale.body
                        radius: TypeScale.body / 2
                        color: categoryRow.categoryColor
                    }

                    Label {
                        Layout.fillWidth: true
                        text: categoryRow.name
                        elide: Text.ElideRight
                    }
                }
            }

            onCurrentIndexChanged: {
                if (categoryList.currentIndex >= 0) {
                    nameField.text = root.categories.names[categoryList.currentIndex];
                    colorBox.currentIndex = root.categories.colorIndexAt(categoryList.currentIndex);
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.gridUnit * 3
            rowSpacing: Theme.gridUnit * 2

            Label {
                text: qsTr("Name")
            }

            TextField {
                id: nameField

                objectName: "categoryNameField"
                Layout.fillWidth: true
                Accessible.name: qsTr("Name of the category")
            }

            Label {
                text: qsTr("Colour")
            }

            ComboBox {
                id: colorBox

                objectName: "categoryColorBox"
                Layout.fillWidth: true
                model: root.colorChoices
                Accessible.name: qsTr("Colour of the category")

                FocusRing {
                    shown: colorBox.visualFocus
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gridUnit * 2

            Button {
                id: removeButton

                objectName: "removeCategoryButton"
                text: qsTr("Delete")
                icon.source: Theme.icon("trash")
                flat: true
                enabled: root.canEdit && categoryList.currentIndex >= 0
                onClicked: root.removeRequested(categoryList.currentIndex)

                FocusRing {
                    shown: removeButton.visualFocus
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                id: changeButton

                objectName: "changeCategoryButton"
                text: qsTr("Change")
                enabled: root.canEdit && categoryList.currentIndex >= 0 && nameField.text.trim().length > 0
                onClicked: root.editRequested(categoryList.currentIndex, nameField.text, colorBox.currentIndex)

                FocusRing {
                    shown: changeButton.visualFocus
                }
            }

            Button {
                id: addButton

                objectName: "addCategoryButton"
                text: qsTr("Add")
                icon.source: Theme.icon("plus")
                highlighted: true
                enabled: root.canEdit && nameField.text.trim().length > 0
                onClicked: root.addRequested(nameField.text, colorBox.currentIndex)

                FocusRing {
                    shown: addButton.visualFocus
                }
            }
        }
    }
}
