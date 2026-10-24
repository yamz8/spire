import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Asks before a step that is hard to take back.
Popup {
    id: popup
    objectName: "confirmPopup"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 22
    width: Math.min(parent.width - 48, 440)
    background: Rectangle { color: win.panel; radius: theme.radius; border.color: win.line }

    property string heading
    property string body
    property string action
    property bool destructive: false
    property var onConfirm: null

    function ask(headingText, bodyText, actionText, isDestructive, callback) {
        heading = headingText
        body = bodyText
        action = actionText
        destructive = isDestructive
        onConfirm = callback
        open()
        confirmButton.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 14
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 17
            font.weight: Font.Medium
            text: popup.heading
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: win.muted
            text: popup.body
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button { text: qsTr("Cancel"); flat: true; onClicked: popup.close() }
            Button {
                id: confirmButton
                objectName: "confirmButton"
                text: popup.action
                highlighted: true
                Material.accent: popup.destructive ? theme.danger : theme.accent
                Keys.onReturnPressed: clicked()
                onClicked: {
                    popup.close()
                    if (popup.onConfirm) popup.onConfirm()
                }
            }
        }
    }
}
