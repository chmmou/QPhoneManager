pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    required property CalendarArea area

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 36, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 36)
    height: Math.min(TypeScale.body * 34, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 34)
    modal: true
    Overlay.modal: Scrim {}
    Material.elevation: 0
    title: qsTr("Calendars on the phone")

    footer: DialogButtonBox {
        Button {
            id: applyButton

            objectName: "applyCalendarsButton"
            text: qsTr("Apply")
            enabled: root.area.canEdit && root.area.calendars.modified
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole

            FocusRing {
                shown: applyButton.visualFocus
            }
        }

        Button {
            id: cancelButton

            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole

            FocusRing {
                shown: cancelButton.visualFocus
            }
        }
    }

    onOpened: calendarList.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: root.area.applyCalendarChoice()
    onRejected: root.area.calendars.revertChoice()

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: qsTr("Choose the calendars this computer shows and syncs, and the one new appointments go to. Only calendars the phone may write to can take new appointments.")
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            visible: !root.area.canEdit
            text: qsTr("Connect the phone to change the choice.")
            color: Theme.warning
            wrapMode: Text.Wrap
        }

        ListView {
            id: calendarList

            objectName: "calendarChoiceList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The list scrolls through more calendars than fit.
            clip: true
            model: root.area.calendars
            activeFocusOnTab: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Calendars")

            delegate: Rectangle {
                id: calendarRow

                required property int index
                required property string title
                required property color calendarColor
                required property bool writable
                required property bool chosen
                required property bool isDefault
                required property string account

                width: ListView.view.width
                implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 3
                color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: calendarRow.title
                Accessible.description: calendarRow.writable ? calendarRow.account : qsTr("%1, read only").arg(calendarRow.account)

                RowLayout {
                    id: rowContent

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.gridUnit * 2
                    anchors.rightMargin: Theme.gridUnit * 2
                    spacing: Theme.gridUnit * 2

                    CheckBox {
                        id: chosenBox

                        checked: calendarRow.chosen
                        enabled: root.area.canEdit
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTr("Sync %1").arg(calendarRow.title)
                        onToggled: root.area.calendars.setSelected(calendarRow.index, chosenBox.checked)
                    }

                    Rectangle {
                        Layout.preferredWidth: TypeScale.body
                        Layout.preferredHeight: TypeScale.body
                        radius: TypeScale.body / 2
                        color: calendarRow.calendarColor
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            text: calendarRow.title
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: calendarRow.writable ? calendarRow.account : qsTr("%1, read only").arg(calendarRow.account)
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                            elide: Text.ElideRight
                        }
                    }

                    RadioButton {
                        id: defaultButton

                        text: qsTr("For new")
                        checked: calendarRow.isDefault
                        enabled: root.area.canEdit && calendarRow.writable
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTr("New appointments go to %1").arg(calendarRow.title)
                        onToggled: root.area.calendars.setDefault(calendarRow.index)
                    }
                }
            }

            Keys.onSpacePressed: root.area.calendars.setSelected(calendarList.currentIndex, !root.area.calendars.isSelected(calendarList.currentIndex))
            Keys.onReturnPressed: root.area.calendars.setDefault(calendarList.currentIndex)
            Keys.onEnterPressed: root.area.calendars.setDefault(calendarList.currentIndex)
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Space adds the chosen calendar to the sync or takes it out, Enter makes it the one for new appointments.")
            font.pixelSize: TypeScale.hint
            color: Theme.foregroundOnSurfaceVariant
            wrapMode: Text.Wrap
        }
    }

    FocusRing {
        parent: calendarList
        shown: calendarList.activeFocus
    }
}
