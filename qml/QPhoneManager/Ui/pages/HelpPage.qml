pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    // The text takes the focus once the help has finished loading.
    property bool focusTextWhenReady: false
    // A new link colour reloads the text; these keep the reader's selection
    // and scroll position.
    property bool keepView: false
    property int keptSelectionStart: 0
    property int keptSelectionEnd: 0
    property real keptContentY: 0

    signal aboutRequested

    objectName: "helpPage"

    Binding {
        target: HelpController
        property: "linkColor"
        value: Theme.primary
    }

    Connections {
        target: HelpController

        function onPageChanged(): void {
            contentsList.currentIndex = HelpController.toc.rowOf(HelpController.currentTopic);
            helpText.deselect();
            helpText.cursorPosition = 0;
            (helpScroll.contentItem as Flickable).contentY = 0;
        }

        function onLinkColorChanged(): void {
            root.keepView = HelpController.currentHtml.length > 0;
            root.keptSelectionStart = helpText.selectionStart;
            root.keptSelectionEnd = helpText.selectionEnd;
            root.keptContentY = (helpScroll.contentItem as Flickable).contentY;
        }

        function onBusyChanged(): void {
            if (HelpController.busy || !root.focusTextWhenReady)
                return;
            root.focusTextWhenReady = false;
            if (root.visible)
                root.focusContent();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Pane {
            Layout.fillWidth: true
            padding: Theme.gridUnit * 2
            Material.background: Theme.surface

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Theme.gridUnit

                ToolButton {
                    id: backButton

                    objectName: "helpBackButton"
                    text: qsTr("Back")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("chevron-left")
                    enabled: HelpController.canGoBack
                    Accessible.name: backButton.text
                    ToolTip.visible: backButton.hovered
                    ToolTip.text: backButton.text
                    onClicked: HelpController.back()

                    FocusRing {
                        shown: backButton.visualFocus
                    }
                }

                ToolButton {
                    id: forwardButton

                    objectName: "helpForwardButton"
                    text: qsTr("Forward")
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("chevron-right")
                    enabled: HelpController.canGoForward
                    Accessible.name: forwardButton.text
                    ToolTip.visible: forwardButton.hovered
                    ToolTip.text: forwardButton.text
                    onClicked: HelpController.forward()

                    FocusRing {
                        shown: forwardButton.visualFocus
                    }
                }

                Label {
                    objectName: "helpTitle"
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gridUnit * 2
                    text: HelpController.currentTitle
                    textFormat: Text.PlainText
                    font.pixelSize: TypeScale.section
                    elide: Text.ElideRight
                }

                Button {
                    id: aboutButton

                    objectName: "aboutButton"
                    text: qsTr("About QPhoneManager")
                    icon.source: Theme.icon("info-circle")
                    flat: true
                    onClicked: root.aboutRequested()

                    FocusRing {
                        shown: aboutButton.visualFocus
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outline
        }

        // Selectable, so that the address in it can be copied.
        TextArea {
            id: noticeText

            objectName: "helpNotice"
            Layout.fillWidth: true
            visible: HelpController.notice.length > 0
            readOnly: true
            selectByMouse: true
            selectByKeyboard: true
            activeFocusOnTab: true
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            leftPadding: Theme.gridUnit * 4
            rightPadding: Theme.gridUnit * 4
            text: HelpController.notice
            color: Theme.error
            selectedTextColor: Theme.foregroundOnPrimary
            background: Rectangle {
                color: Theme.surfaceContainer
            }
            Accessible.name: noticeText.text
            onTextChanged: {
                if (noticeText.text.length > 0)
                    noticeText.Accessible.announce(noticeText.text);
            }

            FocusRing {
                shown: noticeText.activeFocus
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: TypeScale.body * 16

                ListView {
                    id: contentsList

                    objectName: "helpContents"
                    anchors.fill: parent
                    // The contents scroll when the window is too low for all chapters.
                    clip: true
                    model: HelpController.toc
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    highlightMoveDuration: 0
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Contents")
                    Accessible.description: qsTr("Enter opens the chapter")
                    ScrollBar.vertical: ThemedScrollBar {}

                    // A plain row: the list carries the focus and moves through the rows.
                    delegate: Rectangle {
                        id: chapter

                        required property int index
                        required property string topic
                        required property string title

                        width: ListView.view.width
                        implicitHeight: chapterLabel.implicitHeight + Theme.gridUnit * 5
                        color: chapter.ListView.isCurrentItem ? Theme.surfaceVariant : "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: chapter.title
                        Accessible.selected: chapter.topic === HelpController.currentTopic

                        Label {
                            id: chapterLabel

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.gridUnit * 4
                            anchors.rightMargin: Theme.gridUnit * 4
                            text: chapter.title
                            textFormat: Text.PlainText
                            font.bold: chapter.topic === HelpController.currentTopic
                            wrapMode: Text.Wrap
                        }

                        TapHandler {
                            onTapped: {
                                contentsList.currentIndex = chapter.index;
                                HelpController.openTopic(chapter.topic);
                            }
                        }

                        CurrentMarker {
                            current: chapter.ListView.isCurrentItem
                            viewFocused: contentsList.activeFocus
                        }
                    }

                    Keys.onReturnPressed: root.openCurrentChapter()
                    Keys.onEnterPressed: root.openCurrentChapter()
                    Keys.onSpacePressed: root.openCurrentChapter()
                }

                FocusRing {
                    objectName: "helpContentsFocusRing"
                    shown: contentsList.activeFocus
                }
            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: Theme.outline
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    objectName: "helpBusy"
                    anchors.centerIn: parent
                    visible: HelpController.busy
                    spacing: Theme.gridUnit * 2

                    BusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: HelpController.busy
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("QPhoneManager is loading the help.")
                    }
                }

                Label {
                    objectName: "helpMessage"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - Theme.gridUnit * 8, TypeScale.body * 28)
                    visible: !HelpController.busy && HelpController.currentHtml.length === 0
                    text: HelpController.message
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                ThemedScrollView {
                    id: helpScroll

                    anchors.fill: parent
                    visible: HelpController.currentHtml.length > 0
                    contentWidth: helpScroll.availableWidth
                    // The text area takes the width of the view on its own, so the room
                    // for the scroll bar is kept free on the side of the bar.
                    leftPadding: helpScroll.mirrored ? helpScroll.ScrollBar.vertical.width : 0
                    rightPadding: helpScroll.mirrored ? 0 : helpScroll.ScrollBar.vertical.width

                    TextArea {
                        id: helpText

                        objectName: "helpText"
                        readOnly: true
                        textFormat: TextEdit.RichText
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        selectByKeyboard: true
                        activeFocusOnTab: true
                        leftPadding: Theme.gridUnit * 6
                        rightPadding: Theme.gridUnit * 6
                        topPadding: Theme.gridUnit * 4
                        bottomPadding: Theme.gridUnit * 4
                        text: HelpController.currentHtml
                        selectedTextColor: Theme.foregroundOnPrimary
                        background: null
                        Accessible.name: HelpController.currentTitle
                        Accessible.description: qsTr("Tab moves to the next link, Enter follows it")
                        onLinkActivated: link => HelpController.openLink(link)
                        onTextChanged: {
                            if (!root.keepView)
                                return;
                            root.keepView = false;
                            helpText.select(root.keptSelectionStart, root.keptSelectionEnd);
                            (helpScroll.contentItem as Flickable).contentY = root.keptContentY;
                        }

                        Keys.onTabPressed: event => {
                            event.accepted = root.selectLink(true);
                        }
                        Keys.onBacktabPressed: event => {
                            event.accepted = root.selectLink(false);
                        }
                        Keys.onReturnPressed: event => {
                            event.accepted = root.followSelectedLink();
                        }
                        Keys.onEnterPressed: event => {
                            event.accepted = root.followSelectedLink();
                        }

                        HoverHandler {
                            cursorShape: helpText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
                        }
                    }
                }

                FocusRing {
                    objectName: "helpTextFocusRing"
                    shown: helpText.activeFocus
                }
            }
        }
    }

    function focusContent(): void {
        root.focusTextWhenReady = HelpController.busy;
        if (helpScroll.visible)
            helpText.forceActiveFocus(Qt.OtherFocusReason);
        else
            contentsList.forceActiveFocus(Qt.OtherFocusReason);
    }

    function openCurrentChapter(): void {
        if (contentsList.currentIndex >= 0)
            HelpController.openTopic(HelpController.toc.topicAt(contentsList.currentIndex));
    }

    // Moves the selection to the next or previous link of the page, so that a
    // link can be followed without the mouse. False at the end of the page,
    // where the tab key leaves the text.
    function selectLink(forward: bool): bool {
        const link = HelpController.nextLink(forward ? helpText.selectionEnd : helpText.selectionStart, forward);
        if (link.end === undefined)
            return false;
        helpText.select(link.start, link.end);
        return true;
    }

    function followSelectedLink(): bool {
        if (helpText.selectedText.length === 0)
            return false;
        const link = HelpController.linkAt(helpText.selectionStart);
        return link.length > 0 && HelpController.openLink(link);
    }
}
