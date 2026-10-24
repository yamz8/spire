import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The project's details, as they head the pay application. Each field is
// saved when it is done being edited.
Popup {
    id: popup
    objectName: "projectPopup"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 24
    width: Math.min(parent.width - 48, 640)
    background: Rectangle { color: win.panel; radius: theme.radius; border.color: win.line }

    readonly property var info: backend.info
    readonly property var fields: [
        { key: "name", label: qsTr("Project name"), wide: true },
        { key: "location", label: qsTr("Project address"), wide: true },
        { key: "owner", label: qsTr("Owner") },
        { key: "contractor", label: qsTr("Contractor") },
        { key: "architect", label: qsTr("Architect") },
        { key: "contractFor", label: qsTr("Contract for") },
        { key: "contractNumber", label: qsTr("Contract no.") },
        { key: "currency", label: qsTr("Currency symbol") },
        { key: "retainageWork", label: qsTr("Retainage on work, %"), number: true },
        { key: "retainageStored", label: qsTr("Retainage on stored, %"), number: true },
    ]

    onOpened: fieldRepeater.itemAt(0).forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: 16
        Label {
            text: qsTr("Project details")
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        GridLayout {
            id: grid
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 4
            Repeater {
                id: fieldRepeater
                model: popup.fields
                delegate: TextField {
                    id: field
                    required property var modelData
                    readonly property string saved: {
                        const value = popup.info[modelData.key]
                        return modelData.number ? (value ?? 0).toLocaleString(Qt.locale(), 'f', 2).replace(new RegExp("\\" + Qt.locale().decimalPoint + "?0+$"), "") : (value ?? "")
                    }
                    Layout.fillWidth: true
                    Layout.columnSpan: modelData.wide ? 2 : 1
                    Layout.preferredWidth: 1
                    placeholderText: win.label(field, modelData.label)
                    text: saved
                    inputMethodHints: modelData.number ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
                    onEditingFinished: {
                        if (text.trim() !== saved) backend.setInfo(modelData.key, text)
                        text = Qt.binding(() => saved)
                    }
                }
            }
            Button {
                objectName: "contractDate"
                Layout.fillWidth: true
                flat: true
                contentItem: ColumnLayout {
                    spacing: 0
                    Label { text: qsTr("Contract date"); color: win.muted; font.pixelSize: 11 }
                    Label { text: popup.info.contractDate && !isNaN(popup.info.contractDate) ? win.date(popup.info.contractDate) : qsTr("Not set"); font.pixelSize: 14 }
                }
                onClicked: datePopup.pick(popup.info.contractDate, (d) => backend.setInfo("contractDate", d))
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: win.muted
            text: qsTr("Retainage set here is used for new applications. Each application keeps its own rates, which you can change on it, for example to reduce retainage at half completion.")
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: backend.projectPath
                color: win.muted
                elide: Text.ElideMiddle
                font.pixelSize: 12
            }
            Button {
                text: qsTr("Done")
                highlighted: true
                onClicked: { popup.forceActiveFocus(); popup.close() }
            }
        }
    }
}
