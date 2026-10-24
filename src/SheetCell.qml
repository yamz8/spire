import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One cell of the continuation sheet: its value, or a field to type a new
// one while it is being edited.
Item {
    id: cell

    property var column
    property var row
    property int lineId: 0

    readonly property string key: column.key
    readonly property bool editable: sheet.canEdit(row, key)
    readonly property bool editing: sheet.editId === lineId && sheet.editKey === key
    readonly property bool billingCell: sheet.billingKeys.indexOf(key) >= 0

    readonly property string display: {
        translator.language
        const r = row
        switch (key) {
        case "code": return r.code !== "" ? r.code
                            : r.changeOrder ? qsTr("CO %1").arg(r.coNumber.toLocaleString(Qt.locale(), 'f', 0))
                            : r.number.toLocaleString(Qt.locale(), 'f', 0)
        case "description": return r.description
        case "percent": return win.percent(r.percent)
        case "installed": return r.quantity > 0 ? qsTr("%1 of %2 %3").arg(backend.quantity(r.installed)).arg(backend.quantity(r.quantity)).arg(r.unit).trim() : ""
        default: return win.money(r[key])
        }
    }

    // Editable cells are lightly framed, so it's clear what can be typed in.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: Math.min(4, theme.radius)
        color: cell.editing ? win.raised : cellHover.hovered && cell.editable ? "#202027" : "transparent"
        border.color: cell.editing ? theme.accent : cellHover.hovered && cell.editable ? win.line : "transparent"
        border.width: 1
    }
    HoverHandler { id: cellHover; cursorShape: cell.editable ? Qt.IBeamCursor : Qt.ArrowCursor }
    // An exclusive grab, so the row's own tap (which selects and saves any
    // edit in progress) doesn't close the editor this one opens.
    TapHandler {
        enabled: cell.editable && !cell.editing
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: sheet.edit(cell.lineId, cell.key)
    }

    // The percent complete reads at a glance as a bar under the number.
    Rectangle {
        visible: cell.key === "percent" && !cell.editing
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 7
        anchors.right: parent.right
        anchors.rightMargin: 10
        width: parent.width - 20
        height: 3
        radius: 1.5
        color: "#26262d"
        Rectangle {
            anchors.left: parent.left
            height: parent.height
            radius: 1.5
            width: parent.width * Math.max(0, Math.min(1, cell.row.percent / 100))
            color: cell.row.percent >= 99.995 ? theme.success : theme.accent
        }
    }

    Label {
        id: valueLabel
        visible: !cell.editing
        anchors.fill: parent
        anchors.bottomMargin: cell.key === "percent" ? 6 : 0
        leftPadding: 10
        rightPadding: 10
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: cell.column.end ? Text.AlignRight : Text.AlignLeft
        elide: Text.ElideRight
        textFormat: Text.PlainText
        font.pixelSize: 13
        text: cell.key === "description" && cell.display === "" ? qsTr("Untitled line") : cell.display
        font.italic: cell.key === "description" && cell.display === ""
        font.weight: cell.key === "thisPeriod" && cell.row.thisPeriod !== 0 ? Font.DemiBold : Font.Normal
        color: {
            if (cell.key === "description" && cell.display === "") return win.muted
            if (cell.key === "code") return cell.row.changeOrder ? theme.accent : win.muted
            if (cell.key === "thisPeriod" && cell.row.thisPeriod < 0) return theme.danger
            if (cell.key === "balance" && cell.row.balance < 0) return theme.danger
            if ((cell.key === "previous" || cell.key === "retainage" || cell.key === "stored" || cell.key === "thisPeriod") && cell.row[cell.key] === 0) return win.muted
            return Material.foreground
        }
    }

    Loader {
        anchors.fill: parent
        anchors.margins: 4
        active: cell.editing
        sourceComponent: TextField {
            objectName: "cellEditor"
            leftPadding: 6
            rightPadding: 6
            topPadding: 0
            bottomPadding: 0
            horizontalAlignment: cell.column.end ? Text.AlignRight : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            selectByMouse: true
            background: null
            inputMethodHints: cell.key === "code" || cell.key === "description" ? Qt.ImhNone : Qt.ImhFormattedNumbersOnly
            Component.onCompleted: {
                text = sheet.editorText(cell.row, cell.key)
                sheet.draft = text
                forceActiveFocus()
                selectAll()
            }
            onTextChanged: if (cell.editing) sheet.draft = text
            onAccepted: sheet.moveEdit(1, 0)
            onActiveFocusChanged: if (!activeFocus && cell.editing) sheet.commit()
            Keys.onTabPressed: sheet.moveEdit(0, 1)
            Keys.onBacktabPressed: sheet.moveEdit(0, -1)
            Keys.onUpPressed: sheet.moveEdit(-1, 0)
            Keys.onDownPressed: sheet.moveEdit(1, 0)
        }
    }

    // A value cut short shows in full on hover.
    ToolTip.visible: cellHover.hovered && valueLabel.truncated && !cell.editing
    ToolTip.delay: 500
    ToolTip.text: cell.display
}
