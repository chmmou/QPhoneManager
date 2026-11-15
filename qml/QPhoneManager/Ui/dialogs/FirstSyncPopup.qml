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
            text: qsTr("%1 (%2 of %3)").arg(root.areaName).arg(root.step).arg(root.stepCount)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            onTextChanged: {
                if (root.running && root.areaName.length > 0)
                    areaLabel.Accessible.announce(areaLabel.text);
            }
        }

        ProgressBar {
            objectName: "firstSyncProgress"
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, root.stepCount)
            value: Math.max(0, root.step - 1)
            Accessible.name: qsTr("Progress of the first sync")
        }
    }
}
