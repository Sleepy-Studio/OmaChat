import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OmaChat

// Picks people for a new group conversation, or to add to an existing one.
// Everyone listed shares a server with you; the server allows 3-10 people
// per group, you included.
Dialog {
    id: dialog

    property string channelId // empty: create a new group
    property var exclude: []
    property var picked: []
    readonly property bool creating: channelId.length === 0
    readonly property int maxPick: creating ? 9 : Math.max(0, 10 - exclude.length)
    title: creating ? qsTr("New group conversation") : qsTr("Add people")
    height: Math.min(Theme.px(560), (parent ? parent.height : 600) - Theme.px(40))

    function openCreate() {
        channelId = ""
        exclude = []
        open()
    }
    function openAdd(id, recipients) {
        channelId = id
        exclude = recipients
        open()
    }
    function toggle(userId, on) {
        const next = picked.filter(u => u !== userId)
        if (on && next.length < maxPick)
            next.push(userId)
        picked = next
    }

    onAboutToShow: {
        picked = []
        filter.text = ""
        name.text = ""
        people.model = App.knownUsers().filter(u => dialog.exclude.indexOf(u.userId) < 0)
    }
    onOpened: filter.input.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: Theme.px(10)

        Text { text: dialog.title; color: Theme.text; font.pixelSize: Theme.px(16); font.bold: true }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.pixelSize: Theme.px(12)
            text: dialog.creating ? qsTr("Pick 2 to 9 people you share a server with.")
                                  : qsTr("You can add %n more.", "", dialog.maxPick)
        }
        Field {
            id: filter
            Layout.fillWidth: true
            placeholder: qsTr("Search people")
        }
        ListView {
            id: people
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.px(2)
            delegate: Rectangle {
                id: person
                required property var modelData
                readonly property bool matches: filter.text.length === 0
                    || modelData.name.toLowerCase().indexOf(filter.text.toLowerCase()) >= 0
                    || modelData.username.toLowerCase().indexOf(filter.text.toLowerCase()) >= 0
                readonly property bool chosen: dialog.picked.indexOf(modelData.userId) >= 0
                visible: matches
                width: ListView.view.width
                height: matches ? Theme.px(36) : 0
                radius: Theme.px(4)
                color: chosen ? Theme.selection : pickArea.containsMouse ? Theme.raised : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.px(8)
                    anchors.rightMargin: Theme.px(8)
                    spacing: Theme.px(8)
                    Avatar { userId: person.modelData.userId; name: person.modelData.name; status: person.modelData.status; size: Theme.px(24) }
                    Text {
                        Layout.fillWidth: true
                        text: person.modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.px(13)
                        elide: Text.ElideRight
                    }
                    Text { text: "@" + person.modelData.username; color: Theme.textFaint; font.pixelSize: Theme.px(11) }
                    Check {
                        checked: person.chosen
                        enabled: person.chosen || dialog.picked.length < dialog.maxPick
                        onToggled: dialog.toggle(person.modelData.userId, checked)
                    }
                }
                MouseArea {
                    id: pickArea
                    anchors.fill: parent
                    anchors.rightMargin: Theme.px(36)
                    hoverEnabled: true
                    onClicked: dialog.toggle(person.modelData.userId, !person.chosen)
                }
            }
        }
        Field {
            id: name
            visible: dialog.creating
            Layout.fillWidth: true
            label: qsTr("Name (optional)")
            placeholder: qsTr("Named after its people if empty")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.px(8)
            FlatButton { text: qsTr("Cancel"); onClicked: dialog.close() }
            FlatButton {
                primary: true
                text: dialog.creating ? qsTr("Start conversation") : qsTr("Add")
                enabled: dialog.creating ? dialog.picked.length >= 2 : dialog.picked.length >= 1
                onClicked: {
                    if (dialog.creating)
                        App.createGroup(dialog.picked, name.text)
                    else
                        App.addToGroup(dialog.channelId, dialog.picked)
                    dialog.close()
                }
            }
        }
    }
}
