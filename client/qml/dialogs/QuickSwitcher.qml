import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Ctrl+K: jump to any channel, voice channel, server or person. Fully
// keyboard driven: type, Up/Down, Enter, Esc.
Dialog {
    id: dialog
    objectName: "quickSwitcher"
    title: qsTr("Search OmaChat")
    width: Math.min(Metrics.px(560), (parent ? parent.width : 800) - Metrics.px(40))
    y: Metrics.px(16)
    anchors.centerIn: undefined
    x: ((parent ? parent.width : 800) - width) / 2
    padding: Metrics.px(12)

    onAboutToShow: {
        query.text = ""
        App.switcherQuery("")
        results.currentIndex = 0
    }
    onOpened: query.forceActiveFocus()

    function activate(i) {
        if (i < 0 || i >= results.count)
            return
        close()
        App.switcherActivate(i)
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(8)

        TextField {
            id: query
            Layout.fillWidth: true
            implicitHeight: Metrics.px(40)
            placeholderText: qsTr("Where would you like to go?")
            placeholderTextColor: Theme.textFaint
            color: Theme.text
            font.pixelSize: Metrics.px(16)
            leftPadding: Metrics.px(12)
            Accessible.name: dialog.title
            background: Rectangle { radius: Metrics.px(6); color: Theme.surfaceAlt; border.color: Theme.border }
            onTextChanged: {
                App.switcherQuery(text)
                results.currentIndex = 0
            }
            Keys.onDownPressed: results.currentIndex = Math.min(results.currentIndex + 1, results.count - 1)
            Keys.onUpPressed: results.currentIndex = Math.max(results.currentIndex - 1, 0)
            Keys.onReturnPressed: dialog.activate(results.currentIndex)
            Keys.onEnterPressed: dialog.activate(results.currentIndex)
        }

        ListView {
            id: results
            objectName: "quickSwitcherResults"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Metrics.px(380),
                Math.max(Metrics.px(36), (dialog.parent ? dialog.parent.height : 600)
                    - dialog.y - dialog.padding * 2 - Metrics.px(90)))
            model: App.switcher
            clip: true
            highlightMoveDuration: 0
            Accessible.role: Accessible.List

            delegate: Rectangle {
                id: row
                required property int index
                required property string kind
                required property string label
                required property string detail
                required property string itemId
                property string iconId: ""
                readonly property bool inViewport: dialog.visible && y + height > results.contentY
                    && y < results.contentY + results.height
                function refreshArtwork() {
                    const channel = kind === "text" || kind === "voice" ? App.channelDetails(itemId) : ({})
                    iconId = channel.icon_attachment_id || ""
                    // ListView positions newly created buffered delegates after
                    // construction. Check their final viewport position first.
                    Qt.callLater(row.loadArtwork)
                }
                function loadArtwork() {
                    if (inViewport && iconId.length > 0 && iconId !== "0")
                        App.requestPreview(iconId, "channel-icon.png", 0)
                }
                Component.onCompleted: refreshArtwork()
                onItemIdChanged: refreshArtwork()
                onKindChanged: refreshArtwork()
                onInViewportChanged: Qt.callLater(row.loadArtwork)
                Connections {
                    target: App
                    function onChannelDataChanged(id) { if (id === row.itemId) row.refreshArtwork() }
                    function onArtworkGenerationChanged() { row.refreshArtwork() }
                }
                objectName: "quickSwitcherRow_" + itemId
                width: ListView.view.width
                height: Metrics.px(36)
                radius: Metrics.px(4)
                color: ListView.isCurrentItem ? Theme.selection : (area.containsMouse ? Theme.raised : "transparent")
                Accessible.role: Accessible.ListItem
                Accessible.name: label + ", " + detail
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Metrics.px(10)
                    anchors.rightMargin: Metrics.px(10)
                    spacing: Metrics.px(10)
                    Item {
                        Layout.preferredWidth: Metrics.px(16)
                        Layout.preferredHeight: Metrics.px(16)
                        Icon {
                            objectName: "quickSwitcherIconFallback"
                            anchors.centerIn: parent
                            visible: channelImage.status !== Image.Ready
                            name: row.kind === "voice" ? "speaker" : row.kind === "dm" || row.kind === "user" ? "at"
                                : row.kind === "server" ? "server" : "hash"
                            size: Metrics.px(16)
                            Accessible.ignored: true
                        }
                        Image {
                            id: channelImage
                            objectName: "quickSwitcherChannelIcon"
                            anchors.fill: parent
                            visible: status === Image.Ready
                            source: row.inViewport && row.iconId.length > 0 && row.iconId !== "0"
                                ? App.previews[row.iconId] || "" : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            Accessible.ignored: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.label
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: Metrics.px(14)
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.maximumWidth: row.width * 0.35
                        text: row.detail
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textFaint
                        font.pixelSize: Metrics.px(12)
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: dialog.activate(row.index)
                }
            }
        }

        Text {
            visible: results.count === 0
            text: qsTr("No matches")
            color: Theme.textFaint
            font.pixelSize: Metrics.px(13)
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("↑↓ to navigate · Enter to open · Esc to close")
            color: Theme.textFaint
            font.pixelSize: Metrics.px(11)
        }
    }
}
