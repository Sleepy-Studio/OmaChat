pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Ctrl+F: full-text search of the current channel or, when the server
// supports it, every channel of the current server you can read.
Dialog {
    id: dialog
    objectName: "searchPanel"
    property bool hasSearched: false
    readonly property bool canSearchServer: !App.homeSelected && App.capabilities.indexOf("search.server") >= 0
    property bool wholeServer: false
    readonly property bool searchingServer: wholeServer && canSearchServer
    title: searchingServer ? qsTr("Search %1").arg(App.selectedServerName) : qsTr("Search #%1").arg(App.selectedChannelName)
    width: Math.min(Metrics.px(600), (parent ? parent.width : 800) - Metrics.px(40))
    anchors.centerIn: undefined
    x: ((parent ? parent.width : 800) - width) / 2
    y: Metrics.px(70)
    height: Math.min(implicitHeight, Math.max(0, (parent ? parent.height : 600) - y - Metrics.px(20)))

    onAboutToShow: {
        query.text = ""
        hasSearched = false
        App.clearSearch()
    }
    onOpened: query.forceActiveFocus()

    function submit() {
        if (query.text.trim().length === 0 || App.searchBusy)
            return
        hasSearched = true
        App.search(query.text, dialog.searchingServer)
    }

    function activate(index) {
        if (index < 0 || index >= App.searchResults.count)
            return
        const row = App.searchResults.get(index)
        if (row.itemId && App.openSearchResult(row.itemId))
            dialog.close()
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(8)
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "search" }
            TextField {
                id: query
                objectName: "searchQuery"
                Layout.fillWidth: true
                implicitHeight: Metrics.px(36)
                placeholderText: dialog.title
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                font.pixelSize: Metrics.px(14)
                Accessible.name: dialog.title
                background: Rectangle {
                    radius: Metrics.px(6)
                    color: Theme.surfaceAlt
                    border.color: query.activeFocus ? Theme.focus : Theme.controlBorder
                    border.width: query.activeFocus ? Metrics.px(2) : 1
                }
                onTextChanged: {
                    dialog.hasSearched = false
                    App.clearSearch()
                }
                onAccepted: dialog.submit()
                Keys.onDownPressed: {
                    if (results.count > 0) {
                        results.currentIndex = 0
                        results.forceActiveFocus()
                    }
                }
            }
            FlatButton {
                text: qsTr("Search")
                primary: true
                enabled: query.text.trim().length > 0 && !App.searchBusy
                onClicked: dialog.submit()
            }
        }
        RowLayout {
            visible: dialog.canSearchServer
            spacing: Metrics.px(6)
            Repeater {
                model: [{ label: qsTr("This channel"), server: false }, { label: qsTr("Whole server"), server: true }]
                delegate: FlatButton {
                    required property var modelData
                    text: modelData.label
                    primary: dialog.wholeServer === modelData.server
                    onClicked: {
                        dialog.wholeServer = modelData.server
                        if (query.text.trim().length > 0 && dialog.hasSearched)
                            App.search(query.text, dialog.searchingServer)
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.hasSearched
            text: App.searchBusy ? qsTr("Searching…")
                : App.searchError.length > 0 ? qsTr("Search failed: %1. Try again.").arg(App.searchError)
                : App.searchResults.count === 0 ? qsTr("No matching messages")
                : qsTr("%n result(s). Choose one to view it with earlier messages.", "", App.searchResults.count)
            color: App.searchError.length > 0 ? Theme.danger : Theme.textMuted
            font.pixelSize: Metrics.px(12)
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        ListView {
            id: results
            objectName: "searchResultsList"
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Math.max(contentHeight, Metrics.px(80)), Metrics.px(420))
            model: App.searchResults
            clip: true
            spacing: Metrics.px(2)
            activeFocusOnTab: count > 0
            keyNavigationEnabled: true
            Keys.onReturnPressed: dialog.activate(currentIndex)
            Keys.onEnterPressed: dialog.activate(currentIndex)
            Keys.onSpacePressed: dialog.activate(currentIndex)
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Search results")
            delegate: Rectangle {
                id: result
                required property string itemId
                required property string channelId
                required property string channel
                required property string author
                required property string preview
                required property string time
                required property int index
                width: ListView.view.width
                implicitHeight: col.implicitHeight + Metrics.px(12)
                radius: Metrics.px(4)
                color: result.ListView.isCurrentItem && results.activeFocus ? Theme.selection
                     : area.containsMouse ? Theme.raised : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: author + ", " + time + ": " + preview
                Column {
                    id: col
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Metrics.px(8)
                    RowLayout {
                        objectName: "searchMetadata"
                        width: parent.width
                        spacing: Metrics.px(8)
                        Text {
                            objectName: "searchAuthor"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: result.author
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: Metrics.px(13)
                        }
                        Text {
                            objectName: "searchChannel"
                            Layout.maximumWidth: parent.width * 0.35
                            Layout.minimumWidth: 0
                            visible: dialog.searchingServer && result.channel.length > 0
                            text: "#" + result.channel
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.accent
                            font.pixelSize: Metrics.px(12)
                        }
                        Text { objectName: "searchTime"; text: result.time; color: Theme.textMuted; font.pixelSize: Metrics.px(11) }
                    }
                    Text {
                        width: parent.width
                        text: result.preview
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                        font.pixelSize: Metrics.px(13)
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
                            results.currentIndex = result.index
                            dialog.activate(result.index)
                        }
                    }
                }
                ToolTip.visible: area.containsMouse
                ToolTip.delay: 800
                ToolTip.text: qsTr("Open this message, or right-click to copy its preview")
            }
        }
    }
}
