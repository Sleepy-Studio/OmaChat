import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Emoji picker popup: search across the bundled Unicode catalog (or a
// server's custom emoji, once that lands), grouped by category, with a
// session-local "Recent" row up front. Emits emojiSelected(glyph).
Popup {
    id: root

    signal emojiSelected(string glyph)

    objectName: "emojiPicker"
    parent: Overlay.overlay
    width: Math.min(Metrics.px(320), parent ? parent.width - Metrics.px(24) : Metrics.px(320))
    height: Math.min(Metrics.px(360), parent ? parent.height - Metrics.px(24) : Metrics.px(360))
    x: parent ? Math.max(Metrics.px(12), (parent.width - width) / 2) : 0
    y: parent ? Math.max(Metrics.px(12), (parent.height - height) / 2) : 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var _all: App.emojiCatalog()
    property var _recent: App.recentEmoji()
    property bool _showCustom: false

    readonly property var _filtered: {
        const q = search.text.trim().toLowerCase()
        if (q.length === 0)
            return _all
        return _all.filter(e => e.shortcode.toLowerCase().indexOf(q) >= 0)
    }
    readonly property var _filteredCustom: {
        const q = search.text.trim().toLowerCase()
        const all = App.serverEmoji
        if (q.length === 0)
            return all
        return all.filter(e => e.name.toLowerCase().indexOf(q) >= 0)
    }

    onOpened: {
        search.text = ""
        search.input.forceActiveFocus()
        _recent = App.recentEmoji()
        _showCustom = false
    }

    function pick(glyph) {
        App.noteEmojiUsed(glyph)
        root.emojiSelected(glyph)
        root.close()
    }

    function pickCustom(name) {
        root.emojiSelected(":" + name + ":")
        root.close()
    }

    background: Rectangle {
        radius: Metrics.px(8)
        color: Theme.raised
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Metrics.px(8)

        Field {
            id: search
            Layout.fillWidth: true
            placeholder: qsTr("Search emoji")
            input.objectName: "emojiSearch"
            input.Keys.onDownPressed: {
                const target = root._showCustom ? customGrid : grid
                target.currentIndex = 0
                target.forceActiveFocus()
            }
            input.Keys.onReturnPressed: {
                if (root._showCustom && root._filteredCustom.length > 0) root.pickCustom(root._filteredCustom[0].name)
                else if (!root._showCustom && root._filtered.length > 0) root.pick(root._filtered[0].glyph)
            }
        }

        RowLayout {
            visible: App.serverEmoji.length > 0
            Layout.fillWidth: true
            FlatButton { text: qsTr("Unicode"); primary: !root._showCustom; onClicked: root._showCustom = false }
            FlatButton { text: qsTr("Server"); primary: root._showCustom; onClicked: root._showCustom = true }
        }

        Row {
            visible: !root._showCustom && search.text.length === 0 && root._recent.length > 0
            Layout.fillWidth: true
            spacing: Metrics.px(4)
            SectionLabel { text: qsTr("Recent"); anchors.verticalCenter: parent.verticalCenter }
        }

        Flow {
            visible: !root._showCustom && search.text.length === 0 && root._recent.length > 0
            Layout.fillWidth: true
            spacing: Metrics.px(2)
            Repeater {
                model: root._recent
                delegate: emojiButton
            }
        }

        GridView {
            id: grid
            objectName: "unicodeEmojiGrid"
            activeFocusOnTab: true
            keyNavigationEnabled: true
            Keys.onReturnPressed: if (currentItem) root.pick(currentItem.glyph)
            Keys.onSpacePressed: if (currentItem) root.pick(currentItem.glyph)
            visible: !root._showCustom
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Metrics.px(36)
            cellHeight: Metrics.px(36)
            model: root._showCustom ? [] : root._filtered
            delegate: emojiButton
        }

        GridView {
            id: customGrid
            activeFocusOnTab: true
            keyNavigationEnabled: true
            Keys.onReturnPressed: if (currentItem) root.pickCustom(currentItem.modelData.name)
            Keys.onSpacePressed: if (currentItem) root.pickCustom(currentItem.modelData.name)
            visible: root._showCustom
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Metrics.px(36)
            cellHeight: Metrics.px(36)
            model: root._showCustom ? root._filteredCustom : []
            delegate: customEmojiButton
        }
    }

    Component {
        id: emojiButton
        Rectangle {
            width: Metrics.px(34)
            height: Metrics.px(34)
            radius: Metrics.px(4)
            border.width: GridView.isCurrentItem && grid.activeFocus ? 2 : 0
            border.color: Theme.focus
            color: hover.hovered ? Theme.selection : "transparent"
            readonly property string glyph: typeof modelData === "string" ? modelData : modelData.glyph
            readonly property string tip: typeof modelData === "string" ? "" : modelData.shortcode

            HoverHandler { id: hover }

            Text {
                anchors.centerIn: parent
                font.pixelSize: Metrics.px(20)
                text: parent.glyph
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.pick(parent.glyph)
            }
            ToolTip.visible: hover.hovered && parent.tip.length > 0
            ToolTip.delay: 500
            ToolTip.text: ":" + parent.tip + ":"
        }
    }

    Component {
        id: customEmojiButton
        Rectangle {
            id: cell
            required property var modelData
            width: Metrics.px(34)
            height: Metrics.px(34)
            radius: Metrics.px(4)
            border.width: GridView.isCurrentItem && customGrid.activeFocus ? 2 : 0
            border.color: Theme.focus
            color: chover.hovered ? Theme.selection : "transparent"

            Component.onCompleted: App.requestMedia(modelData.attachment_id, modelData.name)
            HoverHandler { id: chover }

            Image {
                anchors.centerIn: parent
                width: Metrics.px(22)
                height: Metrics.px(22)
                fillMode: Image.PreserveAspectFit
                source: App.previews[cell.modelData.attachment_id] || ""
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.pickCustom(cell.modelData.name)
            }
            ToolTip.visible: chover.hovered
            ToolTip.delay: 500
            ToolTip.text: ":" + cell.modelData.name + ":"
        }
    }
}
