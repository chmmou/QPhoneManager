pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    required property CalendarArea area
    // Row of the alarm in the form; -1 for a new one.
    property int editingRow: -1

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 36, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 36)
    height: Math.min(TypeScale.body * 38, parent ? parent.height - Theme.gridUnit * 8 : TypeScale.body * 38)
    modal: true
    Overlay.modal: Scrim {}
    Material.elevation: 0
    title: qsTr("Alarms on the phone")

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
        root.area.loadAlarms();
        root.startNew();
        closeButton.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: closeButton.focus = false

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            objectName: "alarmStateText"
            Layout.fillWidth: true
            visible: root.area.alarmState !== CalendarArea.AlarmState.Ready
            text: {
                if (!root.area.canEdit)
                    return qsTr("Connect the phone to see its alarms.");
                switch (root.area.alarmState) {
                case CalendarArea.AlarmState.Loading:
                    return qsTr("Loading the alarms of the phone.");
                case CalendarArea.AlarmState.NotSupported:
                    return qsTr("Not supported: this phone does not let the app reach its alarms. Set them on the phone itself.");
                case CalendarArea.AlarmState.Failed:
                    return qsTr("The alarms could not be loaded.");
                default:
                    return "";
                }
            }
            wrapMode: Text.Wrap
        }

        ListView {
            id: alarmList

            objectName: "alarmList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.area.alarmState === CalendarArea.AlarmState.Ready
            // The list scrolls through more alarms than fit.
            clip: true
            model: root.area.alarms
            activeFocusOnTab: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Alarms")

            delegate: Rectangle {
                id: alarmRow

                required property int index
                required property string timeText
                required property string name
                required property string daysText
                required property bool alarmEnabled

                width: ListView.view.width
                implicitHeight: rowContent.implicitHeight + Theme.gridUnit * 3
                color: ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, %2").arg(alarmRow.timeText).arg(alarmRow.daysText)
                Accessible.description: alarmRow.name

                TapHandler {
                    onTapped: {
                        alarmList.currentIndex = alarmRow.index;
                        root.edit(alarmRow.index);
                    }
                }

                RowLayout {
                    id: rowContent

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.gridUnit * 3
                    anchors.rightMargin: Theme.gridUnit * 3
                    spacing: Theme.gridUnit * 3

                    Label {
                        text: alarmRow.timeText
                        font.pixelSize: TypeScale.section
                        color: alarmRow.alarmEnabled ? Theme.foregroundOnSurface : Theme.foregroundOnSurfaceVariant
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            text: alarmRow.name.length > 0 ? alarmRow.name : alarmRow.daysText
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: alarmRow.name.length > 0
                            text: alarmRow.daysText
                            font.pixelSize: TypeScale.hint
                            color: Theme.foregroundOnSurfaceVariant
                            elide: Text.ElideRight
                        }
                    }

                    Label {
                        text: alarmRow.alarmEnabled ? qsTr("On") : qsTr("Off")
                        font.pixelSize: TypeScale.hint
                        color: Theme.foregroundOnSurfaceVariant
                    }
                }
            }

            Keys.onReturnPressed: root.edit(alarmList.currentIndex)
            Keys.onEnterPressed: root.edit(alarmList.currentIndex)
        }

        Label {
            Layout.fillWidth: true
            visible: root.area.alarmState === CalendarArea.AlarmState.Ready
            text: root.editingRow >= 0 ? qsTr("Change the alarm") : qsTr("New alarm")
            font.pixelSize: TypeScale.section
            Accessible.role: Accessible.Heading
        }

        GridLayout {
            Layout.fillWidth: true
            visible: root.area.alarmState === CalendarArea.AlarmState.Ready
            columns: 2
            columnSpacing: Theme.gridUnit * 3
            rowSpacing: Theme.gridUnit * 2

            Label {
                text: qsTr("Time")
            }

            TextField {
                id: timeField

                objectName: "alarmTimeField"
                Layout.fillWidth: true
                // Shown only while empty: the outlined field would float the form
                // of the time above the value like a label.
                placeholderText: timeField.text.length > 0 ? "" : root.area.editor.timeHint
                Accessible.name: qsTr("Time of the alarm")
            }

            Label {
                text: qsTr("Name")
            }

            TextField {
                id: nameField

                objectName: "alarmNameField"
                Layout.fillWidth: true
                Accessible.name: qsTr("Name of the alarm")
            }
        }

        Flow {
            id: dayFlow

            Layout.fillWidth: true
            visible: root.area.alarmState === CalendarArea.AlarmState.Ready
            spacing: Theme.gridUnit

            Repeater {
                id: dayBoxes

                model: 7

                delegate: CheckBox {
                    id: dayBox

                    required property int index

                    text: Qt.locale().dayName((dayBox.index + 1) % 7, Locale.ShortFormat)
                    Accessible.name: Qt.locale().dayName((dayBox.index + 1) % 7, Locale.LongFormat)

                    FocusRing {
                        shown: dayBox.visualFocus
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.area.alarmState === CalendarArea.AlarmState.Ready
            spacing: Theme.gridUnit * 2

            CheckBox {
                id: enabledBox

                objectName: "alarmEnabledBox"
                text: qsTr("Alarm is on")
                checked: true

                FocusRing {
                    shown: enabledBox.visualFocus
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                id: newButton

                text: qsTr("Start new alarm")
                flat: true
                onClicked: root.startNew()

                FocusRing {
                    shown: newButton.visualFocus
                }
            }

            Button {
                id: saveButton

                objectName: "saveAlarmButton"
                text: qsTr("Save")
                icon.source: Theme.icon("check")
                highlighted: true
                enabled: root.area.canEdit && timeField.text.trim().length > 0 && !root.area.busy
                onClicked: root.area.saveAlarm(root.editingRow, timeField.text, nameField.text, root.chosenDays(), enabledBox.checked)

                FocusRing {
                    shown: saveButton.visualFocus
                }
            }
        }
    }

    function startNew(): void {
        root.editingRow = -1;
        timeField.text = "";
        nameField.text = "";
        enabledBox.checked = true;
        for (let i = 0; i < 7; ++i)
            (dayBoxes.itemAt(i) as CheckBox).checked = false;
    }

    function edit(row: int): void {
        if (row < 0)
            return;
        root.editingRow = row;
        timeField.text = root.area.alarmTimeText(row);
        nameField.text = root.area.alarmName(row);
        enabledBox.checked = root.area.alarmEnabled(row);
        const days = root.area.alarmDays(row);
        for (let i = 0; i < 7; ++i)
            (dayBoxes.itemAt(i) as CheckBox).checked = days.indexOf(i + 1) >= 0;
        timeField.forceActiveFocus(Qt.TabFocusReason);
    }

    function chosenDays(): list<int> {
        let days = [];
        for (let i = 0; i < 7; ++i) {
            if ((dayBoxes.itemAt(i) as CheckBox).checked)
                days.push(i + 1);
        }
        return days;
    }
}
