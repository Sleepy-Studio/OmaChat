import QtQuick
import OmaChat

// The caller reserves geometry from attachment identity, before a preview exists.
Rectangle {
    id: root
    property url source
    property bool failed: false
    property int fillMode: Image.PreserveAspectCrop
    readonly property int imageStatus: artwork.status
    signal retry()
    color: Theme.surfaceAlt
    clip: true

    Image {
        id: artwork
        anchors.fill: parent
        source: root.source
        fillMode: root.fillMode
        asynchronous: true
        // Disk entries are bounded and can be replaced after corruption recovery.
        cache: false
        visible: status === Image.Ready
    }
    Column {
        anchors.centerIn: parent
        width: Math.max(0, parent.width - Metrics.px(24))
        visible: artwork.status !== Image.Ready
        spacing: Metrics.px(4)
        Text {
            width: parent.width
            text: root.failed || artwork.status === Image.Error ? qsTr("Artwork unavailable") : qsTr("Loading artwork…")
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: Theme.textMuted
            font.pixelSize: Metrics.px(12)
            Accessible.name: text
        }
        FlatButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.failed
            text: qsTr("Retry")
            onClicked: root.retry()
        }
    }
}
