import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    readonly property DeviceStatusModel status: AppController.status
    readonly property bool showsCards: AppController.connected && root.status.hasData
    readonly property bool loading: AppController.connecting || (AppController.connected && !root.status.hasData)

    signal connectRequested

    objectName: "overviewPage"

    EmptyState {
        objectName: "overviewEmpty"
        anchors.fill: parent
        visible: !root.showsCards && !root.loading
        title: AppController.connectionState === AppController.ConnectionState.Lost ? qsTr("Connection lost.") : qsTr("No phone connected.")
        text: AppController.connectionState === AppController.ConnectionState.Waiting ? qsTr("Waiting for the phone to connect to this computer.") : qsTr("Connect your phone over Wi-Fi, USB or Bluetooth.")
        actionText: qsTr("Connect")
        actionIcon: "plug-connected"
        onActionTriggered: root.connectRequested()
    }

    ColumnLayout {
        objectName: "overviewLoading"
        anchors.centerIn: parent
        visible: root.loading
        spacing: Theme.gridUnit * 3

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: root.loading
            Accessible.name: qsTr("Connecting")
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: AppController.connectionState === AppController.ConnectionState.WaitingForPin ? qsTr("Waiting for the PIN.") : qsTr("Connecting to the phone.")
        }
    }

    ScrollView {
        id: scroller

        anchors.fill: parent
        visible: root.showsCards
        contentWidth: scroller.availableWidth
        contentHeight: cards.implicitHeight + Theme.gridUnit * 12

        GridLayout {
            id: cards

            width: scroller.availableWidth - Theme.gridUnit * 12
            x: Theme.gridUnit * 6
            y: Theme.gridUnit * 6
            columns: Math.max(1, Math.floor(cards.width / (TypeScale.body * 22)))
            columnSpacing: Theme.gridUnit * 4
            rowSpacing: Theme.gridUnit * 4

            OverviewCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("Phone")
                iconName: "device-mobile"

                Label {
                    objectName: "deviceNameValue"
                    Layout.fillWidth: true
                    text: root.status.deviceName
                    font.pixelSize: TypeScale.section
                    wrapMode: Text.Wrap
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Android")
                    value: root.status.androidVersion
                    valueObjectName: "androidValue"
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Phone app")
                    value: root.status.clientVersion.length > 0 ? qsTr("MyPhoneExplorer Client %1").arg(root.status.clientVersion) : ""
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Build")
                    value: root.status.firmware
                }
            }

            OverviewCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("Battery")
                iconName: "battery"

                Label {
                    objectName: "batteryValue"
                    text: root.status.batteryPercent >= 0 ? qsTr("%1 %").arg(root.status.batteryPercent) : qsTr("Not known")
                    font.pixelSize: TypeScale.title
                }

                ProgressBar {
                    Layout.fillWidth: true
                    visible: root.status.batteryPercent >= 0
                    from: 0
                    to: 100
                    value: root.status.batteryPercent
                    Accessible.name: qsTr("Battery level")
                }

                Label {
                    text: root.status.charging ? qsTr("Charging") : qsTr("On battery")
                    color: Theme.foregroundOnSurfaceVariant
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Temperature")
                    value: root.status.batteryTemperature
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Voltage")
                    value: root.status.batteryVoltage
                }
            }

            OverviewCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("Signal")
                iconName: "antenna-bars-5"

                RowLayout {
                    spacing: Theme.gridUnit * 3

                    SignalBars {
                        bars: root.status.signalBars
                    }

                    Label {
                        objectName: "signalValue"
                        text: root.status.signalText.length > 0 ? root.status.signalText : qsTr("Not known")
                        font.pixelSize: TypeScale.section
                    }
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Network")
                    value: root.status.operatorName
                }
            }

            OverviewCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("SIM cards")
                iconName: "device-mobile"

                Repeater {
                    model: root.status.simCards

                    delegate: Label {
                        required property string modelData

                        Layout.fillWidth: true
                        text: modelData
                        wrapMode: Text.Wrap
                    }
                }

                Label {
                    objectName: "simEmpty"
                    Layout.fillWidth: true
                    visible: root.status.simCards.length === 0
                    text: root.status.simPermissionMissing ? qsTr("The phone app may not read the SIM cards. Allow the phone permission for MyPhoneExplorer Client in the Android settings.") : qsTr("No SIM card found.")
                    color: Theme.foregroundOnSurfaceVariant
                    wrapMode: Text.Wrap
                }
            }

            OverviewCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("Storage")
                iconName: "database"

                Label {
                    text: qsTr("Internal storage")
                }

                ProgressBar {
                    Layout.fillWidth: true
                    visible: root.status.internalStorageUsed >= 0
                    value: root.status.internalStorageUsed
                    Accessible.name: qsTr("Used internal storage")
                }

                Label {
                    objectName: "internalStorageValue"
                    text: root.status.internalStorage.length > 0 ? root.status.internalStorage : qsTr("Not known")
                    color: Theme.foregroundOnSurfaceVariant
                }

                Label {
                    visible: root.status.cardStorage.length > 0
                    text: qsTr("Memory card")
                }

                ProgressBar {
                    Layout.fillWidth: true
                    visible: root.status.cardStorageUsed >= 0
                    value: root.status.cardStorageUsed
                    Accessible.name: qsTr("Used space on the memory card")
                }

                Label {
                    visible: root.status.cardStorage.length > 0
                    text: root.status.cardStorage
                    color: Theme.foregroundOnSurfaceVariant
                }
            }

            OverviewCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("Display and processor")
                iconName: "cpu"

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Display")
                    value: root.status.display
                    valueObjectName: "displayValue"
                }

                InfoRow {
                    Layout.fillWidth: true
                    label: qsTr("Processor load")
                    value: root.status.cpuPercent >= 0 ? qsTr("%1 %").arg(root.status.cpuPercent) : ""
                }

                ProgressBar {
                    Layout.fillWidth: true
                    visible: root.status.cpuPercent >= 0
                    from: 0
                    to: 100
                    value: root.status.cpuPercent
                    Accessible.name: qsTr("Processor load")
                }
            }
        }
    }
}
