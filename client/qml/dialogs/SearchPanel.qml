import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Ctrl+F: full-text search of the current channel or, when the server
// supports it, every channel of the current server you can read.
Dialog {
    id: dialog
    readonly property bool canSearchServer: !App.homeSelected && App.capabilities.indexOf("search.server") >= 0
    property bool wholeServer: false
    readonly property bool searchingServer: wholeServer && canSearchServer
    title: searchingServer ? qsTr("Search %1").arg(App.selectedServerName) : qsTr("Search #%1").arg(App.selectedChannelName)
    width: Math.min(Theme.px(600), (parent ? parent.width : 800) - Theme.px(40))
    anchors.centerIn: undefined
    x: ((parent ? parent.width : 800) - width) / 2
    y: Theme.px(70)

    onAboutToShow: {
        query.text = ""
        App.clearSearch()
    }
    onOpened: query.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: Theme.px(8)
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "search" }
            TextField {
                id: query
                Layout.fillWidth: true
                implicitHeight: Theme.px(36)
                placeholderText: dialog.title
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                font.pixelSize: Theme.px(14)
                Accessible.name: dialog.title
                background: Rectangle { radius: Theme.px(6); color: Theme.surfaceAlt; border.color: Theme.border }
                onAccepted: App.search(text, dialog.searchingServer)
            }
            FlatButton { text: qsTr("Search"); primary: true; onClicked: App.search(query.text, dialog.searchingServer) }
        }
        RowLayout {
            visible: dialog.canSearchServer
            spacing: Theme.px(6)
            Repeater {
                model: [{ label: qsTr("This channel"), server: false }, { label: qsTr("Whole server"), server: true }]
                delegate: FlatButton {
                    required property var modelData
                    text: modelData.label
                    primary: dialog.wholeServer === modelData.server
                    onClicked: {
                        dialog.wholeServer = modelData.server
                        if (query.text.trim().length > 0)
                            App.search(query.text, dialog.searchingServer)
                    }
                }
            }
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Theme.px(420))
            model: App.searchResults
            clip: true
            spacing: Theme.px(2)
            delegate: Rectangle {
                id: result
                required property string itemId
                required property string channelId
                required property string channel
                required property string author
                required property string preview
                required property string time
                width: ListView.view.width
                implicitHeight: col.implicitHeight + Theme.px(12)
                radius: Theme.px(4)
                color: area.containsMouse ? Theme.raised : "transparent"
                Column {
                    id: col
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.px(8)
                    Row {
                        spacing: Theme.px(8)
                        Text { text: result.author; color: Theme.text; font.bold: true; font.pixelSize: Theme.px(13) }
                        Text {
                            visible: dialog.searchingServer && result.channel.length > 0
                            text: "#" + result.channel
                            color: Theme.accent
                            font.pixelSize: Theme.px(12)
                        }
                        Text { text: result.time; color: Theme.textFaint; font.pixelSize: Theme.px(11) }
                    }
                    Text {
                        width: parent.width
                        text: result.preview
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.px(13)
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            App.copyText(result.preview)
                        } else {
                            App.selectChannel(result.channelId)
                            dialog.close()
                        }
                    }
                }
                ToolTip.visible: area.containsMouse
                ToolTip.delay: 800
                ToolTip.text: qsTr("Click to open the channel, right-click to copy")
            }
        }
    }
}
