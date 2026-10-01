import QtQuick
import QtQuick.Controls
import OmaChat

// One attachment inside a message. Images and videos show previews;
// audio and other files open with the desktop's default handler.
Item {
    id: root

    property var attachment: ({})
    property real maxWidth: Theme.px(400)

    readonly property bool isImage: String(attachment.mime_type).startsWith("image/")
                                    && attachment.size <= 10 * 1024 * 1024
    readonly property bool isGif: isImage && String(attachment.mime_type).toLowerCase() === "image/gif"
    readonly property bool isAudio: String(attachment.mime_type).startsWith("audio/")
    readonly property bool isVideo: String(attachment.mime_type).startsWith("video/")
    readonly property string previewUrl: App.previews[attachment.id] || ""
    readonly property string thumbnailUrl: App.videoThumbnails[attachment.id] || ""
    readonly property bool thumbnailFailed: App.videoThumbnails[attachment.id] === false

    implicitWidth: isImage ? imageBox.width : (isVideo ? videoBox.width : card.width)
    implicitHeight: isImage ? imageBox.height : (isVideo ? videoBox.height : card.height)

    Component.onCompleted: {
        if (isImage) App.requestPreview(attachment.id, attachment.filename, attachment.size)
        else if (isVideo) App.requestVideoThumbnail(attachment.id, attachment.filename, attachment.size)
    }

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
            source: root.isImage && !root.isGif ? root.previewUrl : ""
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

    // ---------------------------------------------------------------- video
    Rectangle {
        id: videoBox
        visible: root.isVideo
        width: Math.min(Theme.px(400), root.maxWidth)
        height: width * 9 / 16
        radius: Theme.px(6)
        color: Theme.surface
        border.color: Theme.border
        clip: true

        Image {
            anchors.fill: parent
            source: root.isVideo ? root.thumbnailUrl : ""
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
        }

        Text {
            anchors.centerIn: parent
            visible: root.thumbnailUrl.length === 0
            width: parent.width - Theme.px(24)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            color: Theme.textMuted
            font.pixelSize: Theme.px(12)
            text: root.thumbnailFailed ? root.attachment.filename : qsTr("Loading video preview…")
        }

        Rectangle {
            anchors.centerIn: parent
            width: Theme.px(42)
            height: width
            radius: width / 2
            color: Qt.rgba(0, 0, 0, 0.65)
            visible: root.thumbnailUrl.length > 0
            Icon {
                anchors.centerIn: parent
                name: "play"
                size: Theme.px(20)
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: App.openVideoAttachment(root.attachment.id, root.attachment.filename)
        }
        ToolTip.visible: videoHover.hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Open %1 in MPV").arg(root.attachment.filename)
        HoverHandler { id: videoHover }
    }

    // ----------------------------------------------------------------- file
    Rectangle {
        id: card
        visible: !root.isImage && !root.isVideo
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
            name: root.isAudio ? "speaker" : "file"
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
