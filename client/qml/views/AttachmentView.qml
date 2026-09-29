import QtQuick
import QtQuick.Controls
import OmaChat

// One attachment inside a message. Small images load a preview through the
// daemon's cache; everything else is a file card with a save button.
Item {
    id: root

    property var attachment: ({})
    property real maxWidth: Theme.px(400)

    readonly property bool isImage: String(attachment.mime_type).startsWith("image/")
                                    && attachment.size <= 10 * 1024 * 1024
    readonly property bool isAudio: String(attachment.mime_type).startsWith("audio/")
    readonly property bool isVideo: String(attachment.mime_type).startsWith("video/")
    readonly property bool isMedia: isAudio || isVideo
    readonly property string previewUrl: App.previews[attachment.id] || ""

    implicitWidth: isImage ? imageBox.width : (isMedia ? mediaPlayer.implicitWidth : card.width)
    implicitHeight: isImage ? imageBox.height : (isMedia ? mediaPlayer.implicitHeight : card.height)

    Component.onCompleted: if (isImage) App.requestPreview(attachment.id, attachment.filename, attachment.size)

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Attachment %1, %2").arg(attachment.filename).arg(App.formatSize(attachment.size))

    // ---------------------------------------------------------------- image
    Rectangle {
        id: imageBox
        visible: root.isImage
        readonly property real aspect: image.implicitHeight > 0 ? image.implicitWidth / image.implicitHeight : 4 / 3
        width: Math.min(Theme.px(400), root.maxWidth,
                        image.status === Image.Ready ? image.implicitWidth : Theme.px(240))
        height: Math.min(Theme.px(300), width / aspect)
        radius: Theme.px(6)
        color: Theme.surface
        clip: true

        Image {
            id: image
            anchors.fill: parent
            source: root.previewUrl
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            // Bounds decode memory whatever the sender uploaded.
            sourceSize.width: Theme.px(800)
            sourceSize.height: Theme.px(600)
        }

        Text {
            anchors.centerIn: parent
            visible: image.status !== Image.Ready
            width: parent.width - Theme.px(16)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            color: Theme.textFaint
            font.pixelSize: Theme.px(12)
            text: image.status === Image.Error ? qsTr("Cannot show %1").arg(root.attachment.filename)
                                               : root.attachment.filename
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => mouse.button === Qt.RightButton
                       ? App.saveAttachment(root.attachment.id, root.attachment.filename)
                       : App.openAttachment(root.attachment.id, root.attachment.filename, root.attachment.size)
        }
        ToolTip.visible: imageHover.hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("%1 · click to open, right-click to save").arg(root.attachment.filename)
        HoverHandler { id: imageHover }
    }

    // ---------------------------------------------------------------- media
    MediaPlayer {
        id: mediaPlayer
        visible: root.isMedia
        isVideo: root.isVideo
        source: root.previewUrl
        maxWidth: root.maxWidth
        onPlayRequested: App.requestMedia(root.attachment.id, root.attachment.filename)
    }

    // ----------------------------------------------------------------- file
    Rectangle {
        id: card
        visible: !root.isImage && !root.isMedia
        width: Math.min(Theme.px(360), root.maxWidth)
        height: Theme.px(52)
        radius: Theme.px(6)
        color: cardHover.hovered ? Theme.surfaceAlt : Theme.surface
        border.color: Theme.border

        HoverHandler { id: cardHover }

        Icon {
            id: fileIcon
            anchors.left: parent.left
            anchors.leftMargin: Theme.px(12)
            anchors.verticalCenter: parent.verticalCenter
            name: "file"
            size: Theme.px(24)
        }
        Column {
            anchors.left: fileIcon.right
            anchors.leftMargin: Theme.px(10)
            anchors.right: save.left
            anchors.rightMargin: Theme.px(6)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(2)
            Text {
                width: parent.width
                elide: Text.ElideMiddle
                color: Theme.accent
                font.pixelSize: Theme.px(14)
                text: root.attachment.filename
            }
            Text {
                color: Theme.textFaint
                font.pixelSize: Theme.px(11)
                text: App.formatSize(root.attachment.size)
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: App.openAttachment(root.attachment.id, root.attachment.filename, root.attachment.size)
        }
        IconButton {
            id: save
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(8)
            anchors.verticalCenter: parent.verticalCenter
            iconName: "download"
            tip: qsTr("Save to Downloads")
            onClicked: App.saveAttachment(root.attachment.id, root.attachment.filename)
        }
    }
}
