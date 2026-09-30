import QtQuick
import QtQuick.Controls
import OmaChat

// One attachment inside a message. Small images load a preview through the
// daemon's cache; GIFs play inline. Everything else is a file card.
Item {
    id: root

    property var attachment: ({})
    property real maxWidth: Theme.px(400)

    readonly property bool isImage: String(attachment.mime_type).startsWith("image/")
                                    && attachment.size <= 10 * 1024 * 1024
    readonly property bool isGif: isImage && String(attachment.mime_type).toLowerCase() === "image/gif"
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
        readonly property real naturalWidth: root.isGif ? gif.implicitWidth : image.implicitWidth
        readonly property real naturalHeight: root.isGif ? gif.implicitHeight : image.implicitHeight
        readonly property int imageStatus: root.isGif ? gif.status : image.status
        readonly property real aspect: naturalHeight > 0 ? naturalWidth / naturalHeight : 4 / 3
        width: Math.min(Theme.px(400), root.maxWidth, Theme.px(300) * aspect,
                        imageStatus === Image.Ready ? naturalWidth : Theme.px(240))
        height: width / aspect
        radius: Theme.px(6)
        color: Theme.surface
        clip: true

        Image {
            id: image
            anchors.fill: parent
            visible: !root.isGif
            source: root.isGif ? "" : root.previewUrl
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            // A single source dimension bounds the decode without changing
            // the image's aspect ratio.
            sourceSize.width: Theme.px(800)
        }

        AnimatedImage {
            id: gif
            anchors.fill: parent
            visible: root.isGif
            source: root.isGif ? root.previewUrl : ""
            playing: visible && status === Image.Ready
            cache: false
            fillMode: Image.PreserveAspectFit
            // Qt scales GIF frames to sourceSize before PreserveAspectFit.
            // Setting both dimensions would force every GIF to 4:3.
            sourceSize.width: Theme.px(800)
        }

        Text {
            anchors.centerIn: parent
            visible: imageBox.imageStatus !== Image.Ready
            width: parent.width - Theme.px(16)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            color: Theme.textFaint
            font.pixelSize: Theme.px(12)
            text: imageBox.imageStatus === Image.Error ? qsTr("Cannot show %1").arg(root.attachment.filename)
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
        filename: root.attachment.filename || ""
        source: root.previewUrl
        maxWidth: root.maxWidth
        onPlayRequested: App.requestMedia(root.attachment.id, root.attachment.filename)
        onOpenRequested: App.openVideoAttachment(root.attachment.id, root.attachment.filename)
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
