import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Shows the progress of the first sync of a phone. Open exactly while
// `running` is true; neither a key nor a click closes it.
Dialog {
    id: root

    property bool running: false
    /// Number of the area syncing right now, from 1.
    property int step: 0
    property int stepCount: 0
    /// Translated name of the area syncing right now.
    property string areaName: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(TypeScale.body * 30, parent ? parent.width - Theme.gridUnit * 8 : TypeScale.body * 30)
    visible: root.running
    modal: true
    focus: true
    Overlay.modal: Scrim {}
    // The raised look of Material draws its dialog through a shader effect,
    // which the software renderer leaves out together with the background.
    Material.elevation: 0
    closePolicy: Popup.NoAutoClose
    title: qsTr("First sync")

    Component.onCompleted: Theme.calmPopup(root)
    onRunningChanged: root.takeProgress()
    onStepChanged: root.takeProgress()
    onStepCountChanged: root.takeProgress()
    onAreaNameChanged: root.takeProgress()

    // The values on screen. They stay at the last progress while the popup
    // fades out, because the progress is reset when the sync ends.
    QtObject {
        id: shown

        property int step: 0
        property int stepCount: 0
        property string areaName: ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gridUnit * 3

        Label {
            Layout.fillWidth: true
            text: qsTr("QPhoneManager loads the data of the phone for the first time. This can take a few minutes.")
            wrapMode: Text.Wrap
        }

        Label {
            id: areaLabel

            objectName: "firstSyncArea"
            Layout.fillWidth: true
            text: qsTr("%1 (%2 of %3)").arg(shown.areaName).arg(shown.step).arg(shown.stepCount)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            // Step and area change one after the other; only the text they
            // end up with is announced.
            onTextChanged: Qt.callLater(root.announceArea)
        }

        ProgressBar {
            objectName: "firstSyncProgress"
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, shown.stepCount)
            value: Math.max(0, shown.step - 1)
            Accessible.name: qsTr("Progress of the first sync")
        }
    }

    // Takes the progress only while it is complete; a value already reset at
    // the end of the sync does not reach the screen.
    function takeProgress(): void {
        if (!root.running || root.step < 1 || root.stepCount < 1 || root.areaName.length === 0)
            return;
        shown.step = root.step;
        shown.stepCount = root.stepCount;
        shown.areaName = root.areaName;
    }

    function announceArea(): void {
        if (root.running)
            areaLabel.Accessible.announce(areaLabel.text);
    }
}
