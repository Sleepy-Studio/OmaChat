import QtQuick
import QtQuick.Controls.impl as Impl
import OmaChat

// Monochrome SVG icon tinted from the theme.
Item {
    id: root

    property string name
    property real size: Theme.px(18)
    property color color: Theme.textMuted

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    Accessible.ignored: true

    Impl.IconImage {
        anchors.fill: parent
        source: root.name.length > 0 ? "qrc:/qt/qml/OmaChat/icons/" + root.name + ".svg" : ""
        sourceSize: Qt.size(root.size, root.size)
        color: root.color
        fillMode: Image.PreserveAspectFit
    }
}
