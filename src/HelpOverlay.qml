import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: overlay
    color: Qt.rgba(0, 0, 0, 0.72)

    readonly property var shortcuts: [
        ["Space", qsTr("Edit the selected line's percent complete")],
        ["Enter", qsTr("Save and move down")],
        ["Tab", qsTr("Save and move across")],
        ["Esc", qsTr("Put the value back, then deselect")],
        ["↑ ↓  J K", qsTr("Previous or next line")],
        ["F", qsTr("Bill the line in full")],
        ["A", qsTr("Add a line")],
        ["Alt + ↑ ↓", qsTr("Move the line up or down")],
        ["Delete", qsTr("Delete the line")],
        ["C", qsTr("Add a change order")],
        ["1  2", qsTr("Continuation sheet, change orders")],
        ["[  ]", qsTr("Previous or next application")],
        ["N", qsTr("Start the next application")],
        ["I", qsTr("Import a Plumb takeoff")],
        ["Ctrl + E", qsTr("Export the pay application")],
        ["Ctrl + ,", qsTr("Project details")],
        ["Ctrl + N", qsTr("New project")],
        ["Ctrl + O", qsTr("Open project")],
        ["Ctrl + Z", qsTr("Undo")],
        ["Ctrl + Shift + Z", qsTr("Redo")],
        ["?", qsTr("Show or hide this help")],
        ["Q", qsTr("Quit")],
    ]

    MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 600)
        height: Math.min(parent.height - 48, content.implicitHeight + 48)
        color: win.panel
        radius: theme.radius
        border.color: win.line

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14
            Label {
                text: qsTr("Keyboard shortcuts")
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 24
                rowSpacing: 7
                Repeater {
                    model: overlay.shortcuts.length * 2
                    delegate: Label {
                        required property int index
                        readonly property var entry: overlay.shortcuts[Math.floor(index / 2)]
                        Layout.fillWidth: index % 2 === 1
                        text: entry[index % 2]
                        color: index % 2 === 0 ? theme.accent : Material.foreground
                        font.family: index % 2 === 0 ? "monospace" : Qt.application.font.family
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
