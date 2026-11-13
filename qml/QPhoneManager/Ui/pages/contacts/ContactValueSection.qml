pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property ContactValueModel entries
    required property string title
    required property string addText
    required property string valueName
    property bool isAddress: false
    property string placeholder: ""

    spacing: Theme.gridUnit * 2

    Label {
        text: root.title
        font.pixelSize: TypeScale.section
        Accessible.role: Accessible.Heading
    }

    Repeater {
        model: root.entries

        delegate: RowLayout {
            id: entryRow

            required property int index
            required property string value
            required property int typeIndex
            required property string label
            required property string poBox
            required property string street
            required property string postcode
            required property string city
            required property string region
            required property string country

            Layout.fillWidth: true
            spacing: Theme.gridUnit * 2

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: false
                Layout.preferredWidth: TypeScale.body * 11
                spacing: Theme.gridUnit

                ComboBox {
                    id: typeBox

                    Layout.fillWidth: true
                    model: root.entries.typeNames
                    currentIndex: entryRow.typeIndex
                    Accessible.name: qsTr("Type of %1").arg(root.valueName)
                    onActivated: index => root.entries.setTypeIndex(entryRow.index, index)
                    Component.onCompleted: Theme.calmPopup(typeBox.popup)

                    FocusRing {
                        shown: typeBox.visualFocus
                    }
                }

                TextField {
                    Layout.fillWidth: true
                    visible: entryRow.typeIndex === root.entries.customTypeIndex
                    text: entryRow.label
                    placeholderText: qsTr("Name of the type")
                    Accessible.name: qsTr("Own name of the type")
                    onTextEdited: root.entries.setText(entryRow.index, "label", text)
                }
            }

            TextField {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                visible: !root.isAddress
                text: entryRow.value
                placeholderText: root.placeholder
                Accessible.name: root.valueName
                onTextEdited: root.entries.setText(entryRow.index, "value", text)
            }

            GridLayout {
                Layout.fillWidth: true
                visible: root.isAddress
                columns: 2
                columnSpacing: Theme.gridUnit * 2
                rowSpacing: Theme.gridUnit

                TextField {
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    text: entryRow.street
                    placeholderText: qsTr("Street and number")
                    Accessible.name: qsTr("Street and number")
                    onTextEdited: root.entries.setText(entryRow.index, "street", text)
                }

                TextField {
                    Layout.preferredWidth: TypeScale.body * 6
                    text: entryRow.postcode
                    placeholderText: qsTr("Postcode")
                    Accessible.name: qsTr("Postcode")
                    onTextEdited: root.entries.setText(entryRow.index, "postcode", text)
                }

                TextField {
                    Layout.fillWidth: true
                    text: entryRow.city
                    placeholderText: qsTr("City")
                    Accessible.name: qsTr("City")
                    onTextEdited: root.entries.setText(entryRow.index, "city", text)
                }

                TextField {
                    Layout.fillWidth: true
                    text: entryRow.region
                    placeholderText: qsTr("State or region")
                    Accessible.name: qsTr("State or region")
                    onTextEdited: root.entries.setText(entryRow.index, "region", text)
                }

                TextField {
                    Layout.fillWidth: true
                    text: entryRow.country
                    placeholderText: qsTr("Country")
                    Accessible.name: qsTr("Country")
                    onTextEdited: root.entries.setText(entryRow.index, "country", text)
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    text: entryRow.poBox
                    placeholderText: qsTr("Post office box")
                    Accessible.name: qsTr("Post office box")
                    onTextEdited: root.entries.setText(entryRow.index, "poBox", text)
                }
            }

            ToolButton {
                id: removeButton

                Layout.alignment: Qt.AlignTop
                text: qsTr("Remove %1").arg(root.valueName)
                display: AbstractButton.IconOnly
                icon.source: Theme.icon("x")
                Accessible.name: removeButton.text
                ToolTip.visible: removeButton.hovered
                ToolTip.text: removeButton.text
                onClicked: root.entries.remove(entryRow.index)

                FocusRing {
                    shown: removeButton.visualFocus
                }
            }
        }
    }

    Button {
        id: addButton

        text: root.addText
        icon.source: Theme.icon("plus")
        flat: true
        enabled: !root.entries.full
        onClicked: root.entries.append()

        FocusRing {
            shown: addButton.visualFocus
        }
    }
}
