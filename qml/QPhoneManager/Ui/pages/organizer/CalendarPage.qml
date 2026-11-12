pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    readonly property CalendarArea area: AppController.calendar
    required property string emptyText
    property bool editing: false
    property int pendingDelete: 0
    property string pendingTitle: ""
    readonly property list<string> holidayRegions: ["", "de", "at", "ch"]

    signal connectRequested

    objectName: "calendarPage"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: root.area.hasPhone
            padding: Theme.gridUnit * 2
            Material.background: Theme.surface

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    TabBar {
                        id: viewTabs

                        objectName: "calendarViewTabs"
                        currentIndex: root.area.viewIndex
                        Accessible.name: qsTr("View")
                        onCurrentIndexChanged: root.area.viewIndex = viewTabs.currentIndex

                        TabButton {
                            id: monthTab

                            objectName: "monthTab"
                            width: implicitWidth
                            text: qsTr("Month")

                            FocusRing {
                                shown: monthTab.visualFocus
                            }
                        }

                        TabButton {
                            id: weekTab

                            objectName: "weekTab"
                            width: implicitWidth
                            text: qsTr("Week")

                            FocusRing {
                                shown: weekTab.visualFocus
                            }
                        }

                        TabButton {
                            id: dayTab

                            objectName: "dayTab"
                            width: implicitWidth
                            text: qsTr("Day")

                            FocusRing {
                                shown: dayTab.visualFocus
                            }
                        }

                        TabButton {
                            id: listTab

                            objectName: "listTab"
                            width: implicitWidth
                            text: qsTr("List")

                            FocusRing {
                                shown: listTab.visualFocus
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    ToolButton {
                        id: newButton

                        objectName: "newEventButton"
                        text: qsTr("New appointment")
                        display: AbstractButton.IconOnly
                        icon.source: Theme.icon("plus")
                        enabled: root.area.canEdit
                        Accessible.name: newButton.text
                        ToolTip.visible: newButton.hovered
                        ToolTip.text: newButton.text
                        onClicked: root.startNew()

                        FocusRing {
                            shown: newButton.visualFocus
                        }
                    }

                    ToolButton {
                        id: importButton

                        objectName: "importCalendarButton"
                        text: qsTr("Import appointments and tasks")
                        display: AbstractButton.IconOnly
                        icon.source: Theme.icon("upload")
                        enabled: root.area.canEdit && !root.area.busy
                        Accessible.name: importButton.text
                        ToolTip.visible: importButton.hovered
                        ToolTip.text: importButton.text
                        onClicked: importDialog.open()

                        FocusRing {
                            shown: importButton.visualFocus
                        }
                    }

                    ToolButton {
                        id: exportButton

                        objectName: "exportCalendarButton"
                        text: qsTr("Export appointments and tasks")
                        display: AbstractButton.IconOnly
                        icon.source: Theme.icon("download")
                        enabled: !root.area.busy
                        Accessible.name: exportButton.text
                        ToolTip.visible: exportButton.hovered
                        ToolTip.text: exportButton.text
                        onClicked: exportDialog.open()

                        FocusRing {
                            shown: exportButton.visualFocus
                        }
                    }

                    ToolButton {
                        id: calendarsButton

                        objectName: "chooseCalendarsButton"
                        text: qsTr("Choose calendars")
                        display: AbstractButton.IconOnly
                        icon.source: Theme.icon("filter")
                        enabled: root.area.calendars.count > 0
                        Accessible.name: calendarsButton.text
                        ToolTip.visible: calendarsButton.hovered
                        ToolTip.text: calendarsButton.text
                        onClicked: calendarsDialog.open()

                        FocusRing {
                            shown: calendarsButton.visualFocus
                        }
                    }

                    ToolButton {
                        id: alarmsButton

                        objectName: "alarmsButton"
                        text: qsTr("Alarms")
                        display: AbstractButton.IconOnly
                        icon.source: Theme.icon("clock")
                        enabled: root.area.canEdit
                        Accessible.name: alarmsButton.text
                        ToolTip.visible: alarmsButton.hovered
                        ToolTip.text: alarmsButton.text
                        onClicked: alarmsDialog.open()

                        FocusRing {
                            shown: alarmsButton.visualFocus
                        }
                    }

                    ComboBox {
                        id: holidayBox

                        objectName: "holidayBox"
                        Layout.preferredWidth: TypeScale.body * 10
                        model: [qsTr("No holidays"), qsTr("Germany"), qsTr("Austria"), qsTr("Switzerland")]
                        currentIndex: Math.max(0, root.holidayRegions.indexOf(Settings.holidayRegion))
                        Accessible.name: qsTr("Show public holidays of")
                        ToolTip.visible: holidayBox.hovered
                        ToolTip.text: qsTr("Show public holidays of")
                        onActivated: index => Settings.holidayRegion = root.holidayRegions[index]

                        FocusRing {
                            shown: holidayBox.visualFocus
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gridUnit * 2

                    ToolButton {
                        id: previousButton

                        objectName: "previousPeriodButton"
                        text: qsTr("Earlier")
                        display: AbstractButton.IconOnly
                        icon.source: Theme.icon("chevron-left")
                        Accessible.name: previousButton.text
                        ToolTip.visible: previousButton.hovered
                        ToolTip.text: previousButton.text
                        onClicked: root.area.previous()

                        FocusRing {
                            shown: previousButton.visualFocus
                        }
                    }

                    Button {
                        id: todayButton

                        objectName: "todayButton"
                        text: qsTr("Today")
                        flat: true
                        onClicked: root.area.goToToday()

                        FocusRing {
                            shown: todayButton.visualFocus
                        }
                    }

                    ToolButton {
                        id: nextButton

                        objectName: "nextPeriodButton"
                        text: qsTr("Later")
                        display: AbstractButton.IconOnly
                        icon.source: Theme.icon("chevron-right")
                        Accessible.name: nextButton.text
                        ToolTip.visible: nextButton.hovered
                        ToolTip.text: nextButton.text
                        onClicked: root.area.next()

                        FocusRing {
                            shown: nextButton.visualFocus
                        }
                    }

                    Label {
                        objectName: "calendarPeriodTitle"
                        Layout.fillWidth: true
                        text: root.area.periodTitle
                        font.pixelSize: TypeScale.section
                        elide: Text.ElideRight
                        Accessible.role: Accessible.Heading
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.area.hasPhone
            color: Theme.outline
        }

        ListDetailView {
            id: listDetail

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.area.hasPhone
            listShare: 0.62
            listComponent: Component {
                Loader {
                    sourceComponent: root.area.viewMode === CalendarArea.ViewMode.Month || root.area.viewMode === CalendarArea.ViewMode.Week ? gridComponent : listComponent
                }
            }
            detailComponent: Component {
                Loader {
                    sourceComponent: root.editing ? formComponent : root.area.currentLuid > 0 ? detailViewComponent : dayComponent
                }
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.area.hasPhone
            text: root.emptyText
            actionText: qsTr("Connect")
            actionIcon: "plug-connected"
            onActionTriggered: root.connectRequested()
        }
    }

    Component {
        id: gridComponent

        CalendarGrid {
            area: root.area
            onDayOpened: listDetail.detailOpen = true
        }
    }

    Component {
        id: listComponent

        EventList {
            area: root.area
            showDays: root.area.viewMode === CalendarArea.ViewMode.List
            emptyText: root.area.viewMode === CalendarArea.ViewMode.List ? qsTr("No appointments this month.") : qsTr("No appointments on this day.")
            onOpenRequested: listDetail.detailOpen = true
            onDeleteRequested: luid => root.confirmDelete(luid)
        }
    }

    Component {
        id: dayComponent

        ColumnLayout {
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.gridUnit * 4
                spacing: Theme.gridUnit * 2

                Label {
                    objectName: "dayTitle"
                    Layout.fillWidth: true
                    text: root.area.currentDateText
                    font.pixelSize: TypeScale.section
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                }

                Button {
                    id: newOnDayButton

                    objectName: "newEventOnDayButton"
                    text: qsTr("New appointment")
                    icon.source: Theme.icon("plus")
                    enabled: root.area.canEdit
                    onClicked: root.startNew()

                    FocusRing {
                        shown: newOnDayButton.visualFocus
                    }
                }
            }

            EventList {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.area.viewMode === CalendarArea.ViewMode.Month || root.area.viewMode === CalendarArea.ViewMode.Week
                area: root.area
                emptyText: qsTr("No appointments on this day.")
                onDeleteRequested: luid => root.confirmDelete(luid)
            }

            EmptyState {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.area.viewMode === CalendarArea.ViewMode.Day || root.area.viewMode === CalendarArea.ViewMode.List
                text: qsTr("Select an appointment in the list.")
            }
        }
    }

    Component {
        id: detailViewComponent

        EventDetail {
            area: root.area
            onEditRequested: root.startEdit()
            onDeleteRequested: luid => root.confirmDelete(luid)
        }
    }

    Component {
        id: formComponent

        EventForm {
            id: form

            area: root.area
            onFinished: root.editing = false
            Component.onCompleted: form.focusFirstField()
        }
    }

    Shortcut {
        sequences: [StandardKey.New]
        enabled: root.visible && root.area.canEdit && !root.editing
        onActivated: root.startNew()
    }

    Connections {
        target: root.area.editor

        function onSaved(luid: int): void {
            root.editing = false;
        }

        function onConflictChanged(): void {
            if (root.area.editor.hasConflict)
                conflictDialog.open();
        }
    }

    Connections {
        target: root.area

        function onHasPhoneChanged(): void {
            root.editing = false;
        }
    }

    ConfirmDeleteDialog {
        id: deleteDialog

        objectName: "deleteEventDialog"
        title: qsTr("Delete appointment %1?").arg(root.pendingTitle)
        message: qsTr("The appointment is deleted on the phone and on this computer. A repeating appointment is deleted with all its dates.")
        onAccepted: root.area.removeEvent(root.pendingDelete)
    }

    VersionConflictDialog {
        id: conflictDialog

        objectName: "eventConflictDialog"
        title: qsTr("The appointment was changed on the phone")
        explanation: qsTr("Someone changed this appointment on the phone after it was loaded here. Choose which version to keep.")
        ownText: root.area.editor.ownVersionText
        phoneText: root.area.editor.phoneVersionText
        onKeepOwnRequested: root.area.editor.keepOwnVersion()
        onTakePhoneRequested: root.area.editor.takePhoneVersion()
    }

    CalendarsDialog {
        id: calendarsDialog

        objectName: "calendarsDialog"
        area: root.area
    }

    AlarmsDialog {
        id: alarmsDialog

        objectName: "alarmsDialog"
        area: root.area
    }

    FileDialog {
        id: importDialog

        objectName: "importCalendarDialog"
        title: qsTr("Import appointments and tasks")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("iCalendar (*.ics *.ical *.ifb *.vcs)"), qsTr("All files (*)")]
        onAccepted: root.area.importFrom(importDialog.selectedFile)
    }

    FileDialog {
        id: exportDialog

        objectName: "exportCalendarDialog"
        title: qsTr("Export appointments and tasks")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("iCalendar (*.ics)")]
        defaultSuffix: "ics"
        onAccepted: root.area.exportTo(exportDialog.selectedFile)
    }

    function startNew(): void {
        root.area.editor.newEvent(root.area.currentDate);
        root.editing = true;
        listDetail.detailOpen = true;
    }

    function startEdit(): void {
        if (root.area.editor.load(root.area.currentLuid)) {
            root.editing = true;
            listDetail.detailOpen = true;
        }
    }

    function confirmDelete(luid: int): void {
        if (luid <= 0 || !root.area.canEdit)
            return;
        root.pendingDelete = luid;
        root.pendingTitle = root.area.summaryOf(luid);
        deleteDialog.open();
    }
}
