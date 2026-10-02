import QtQuick
import OmaChat

// Initials avatar with an optional presence dot and speaking ring.
Item {
    id: root

    property string userId
    property string name
    property string status: ""
    property string avatarUrl: {
        App.profilesRevision
        return App.userProfile(userId).avatar_url || ""
    }
    property bool speaking: false
    property real size: Metrics.px(32)

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        id: circle
        anchors.fill: parent
        radius: width / 2
        color: Theme.userColor(root.userId)
        border.width: root.speaking ? Math.max(2, Metrics.px(2)) : 0
        border.color: Theme.success
        clip: true

        Image {
            id: avatarImage
            anchors.fill: parent
            source: root.avatarUrl.startsWith("https://") ? root.avatarUrl : ""
            sourceSize.width: root.size * 2
            sourceSize.height: root.size * 2
            fillMode: Image.PreserveAspectCrop
            visible: status === Image.Ready
        }

        Text {
            anchors.centerIn: parent
            visible: avatarImage.status !== Image.Ready
            text: {
                const parts = root.name.trim().split(/\s+/)
                let s = parts.length > 0 && parts[0].length > 0 ? parts[0][0] : "?"
                if (parts.length > 1 && parts[1].length > 0)
                    s += parts[1][0]
                return s.toUpperCase()
            }
            color: Theme.dark ? "#101010" : "#ffffff"
            font.pixelSize: root.size * 0.4
            font.bold: true
        }
    }

    PresenceDot {
        visible: root.status.length > 0
        status: root.status
        size: Math.max(Metrics.px(9), root.size * 0.32)
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: -1
        anchors.bottomMargin: -1
    }
}
