import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Ctrl+K: jump to any channel, voice channel, server or person. Fully
// keyboard driven: type, Up/Down, Enter, Esc.
Dialog {
    id: dialog
    title: qsTr("Search OmaChat")
    width: Math.min(Metrics.px(560), (parent ? parent.width : 800) - Metrics.px(40))
    y: Metrics.px(80)
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
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Metrics.px(380))
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
                    Icon {
                        name: row.kind === "voice" ? "speaker" : row.kind === "dm" || row.kind === "user" ? "at"
                            : row.kind === "server" ? "server" : "hash"
                        size: Metrics.px(16)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.label
                        color: Theme.text
                        font.pixelSize: Metrics.px(14)
                        elide: Text.ElideRight
                    }
                    Text {
                        text: row.detail
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
