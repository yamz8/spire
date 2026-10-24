import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// What a Plumb takeoff would change, line by line, before anything does.
Popup {
    id: popup
    objectName: "importPopup"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.CloseOnEscape
    padding: 22
    width: Math.min(parent.width - 48, 980)
    height: Math.min(parent.height - 48, 680)
    background: Rectangle { color: win.panel; radius: theme.radius; border.color: win.line }

    readonly property var preview: backend.importPreview
    readonly property var rows: preview.rows ?? []
    readonly property int changing: (preview.updates ?? 0) + (preview.additions ?? 0)
    // Update, add, skip, same.

    onClosed: if (backend.importPreview.rows !== undefined) backend.cancelImport()

    function actionName(action) {
        return [qsTr("Update"), qsTr("Add"), qsTr("Skip"), qsTr("Same")][action] ?? ""
    }
    function actionColor(action) {
        return [theme.accent, theme.success, theme.warning, win.muted][action] ?? win.muted
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: qsTr("Import %1").arg(popup.preview.file ?? "")
            font.pixelSize: 20
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: win.muted
            text: {
                const parts = []
                if (popup.preview.updates) parts.push(qsTr("%n lines updated", "", popup.preview.updates))
                if (popup.preview.additions) parts.push(qsTr("%n lines added", "", popup.preview.additions))
                if (popup.preview.billed) parts.push(qsTr("%n billed on %1", "", popup.preview.billed).arg(win.applicationName(popup.preview.period ?? 0)))
                if (popup.preview.skipped) parts.push(qsTr("%n skipped", "", popup.preview.skipped))
                if (popup.preview.unchanged) parts.push(qsTr("%n already up to date", "", popup.preview.unchanged))
                return parts.join(" · ")
            }
        }
        Label {
            Layout.fillWidth: true
            visible: (popup.preview.warnings ?? []).length > 0
            wrapMode: Text.Wrap
            color: theme.warning
            text: (popup.preview.warnings ?? []).join("\n")
        }

        // Headings.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { Layout.preferredWidth: 70; text: ""; }
            Label { Layout.preferredWidth: 80; text: qsTr("Code"); color: win.muted; font.pixelSize: 12 }
            Label { Layout.fillWidth: true; text: qsTr("Description"); color: win.muted; font.pixelSize: 12 }
            Label { Layout.preferredWidth: 120; text: qsTr("Quantity"); color: win.muted; font.pixelSize: 12; horizontalAlignment: Text.AlignRight }
            Label { Layout.preferredWidth: 130; text: qsTr("Scheduled value"); color: win.muted; font.pixelSize: 12; horizontalAlignment: Text.AlignRight }
            Label { Layout.preferredWidth: 90; text: qsTr("Complete"); color: win.muted; font.pixelSize: 12; horizontalAlignment: Text.AlignRight }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: win.line }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: popup.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                required property var modelData
                width: ListView.view.width
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 70
                        implicitHeight: 22
                        radius: Math.min(4, theme.radius)
                        color: Qt.alpha(popup.actionColor(modelData.action), 0.18)
                        Label {
                            anchors.centerIn: parent
                            text: popup.actionName(modelData.action)
                            color: popup.actionColor(modelData.action)
                            font.pixelSize: 12
                        }
                    }
                    Label { Layout.preferredWidth: 80; text: modelData.code; elide: Text.ElideRight; color: win.muted }
                    Label { Layout.fillWidth: true; text: modelData.description; elide: Text.ElideRight; color: modelData.action === 3 ? win.muted : Material.foreground }
                    Label {
                        Layout.preferredWidth: 120
                        horizontalAlignment: Text.AlignRight
                        text: (backend.quantity(modelData.quantity) + " " + modelData.unit).trim()
                    }
                    Label {
                        Layout.preferredWidth: 130
                        horizontalAlignment: Text.AlignRight
                        text: modelData.action === 2 ? "" : win.money(modelData.scheduled)
                        font.weight: modelData.scheduleChanged ? Font.DemiBold : Font.Normal
                        color: modelData.scheduleChanged ? Material.foreground : win.muted
                    }
                    Label {
                        Layout.preferredWidth: 90
                        horizontalAlignment: Text.AlignRight
                        text: modelData.billing ? win.percent(modelData.percent) : ""
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 80
                    Layout.bottomMargin: 6
                    visible: modelData.note !== ""
                    text: modelData.note
                    color: theme.warning
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button { text: qsTr("Cancel"); flat: true; onClicked: popup.close() }
            Button {
                objectName: "applyImport"
                highlighted: true
                enabled: popup.changing > 0
                text: popup.changing > 0 ? qsTr("Import %n lines", "", popup.changing) : qsTr("Nothing to import")
                onClicked: backend.applyImport()
            }
        }
    }
}
