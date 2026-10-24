import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The continuation sheet for the application on screen: every line of the
// schedule of values with what was billed before, this period, and to date.
// Cells that can change are edited in place, like a spreadsheet: Enter saves
// and moves down, Tab moves across, Esc puts the value back.
Item {
    id: sheet

    property int editId: 0
    property string editKey: ""
    property string draft: ""
    readonly property bool editing: editId > 0

    readonly property var columns: [
        { key: "code", title: qsTr("Item"), width: 64, end: false },
        { key: "description", title: qsTr("Description of work"), width: 0, end: false },
        { key: "scheduled", title: qsTr("Scheduled value"), width: 108, end: true },
        { key: "previous", title: qsTr("Previous"), width: 104, end: true },
        { key: "installed", title: qsTr("Installed"), width: 118, end: true, quantities: true },
        { key: "percent", title: qsTr("% complete"), width: 84, end: true },
        { key: "thisPeriod", title: qsTr("This period"), width: 104, end: true },
        { key: "stored", title: qsTr("Stored"), width: 96, end: true },
        { key: "toDate", title: qsTr("To date"), width: 108, end: true },
        { key: "balance", title: qsTr("Balance"), width: 104, end: true },
        { key: "retainage", title: qsTr("Retainage"), width: 96, end: true },
    ]
    readonly property var shown: columns.filter(c => !c.quantities || backend.hasQuantities)
    readonly property real fixedWidth: shown.reduce((sum, c) => sum + c.width, 0)
    readonly property real tableWidth: Math.max(flick.width, fixedWidth + 150)
    readonly property real descriptionWidth: tableWidth - fixedWidth
    readonly property var billingKeys: ["installed", "percent", "thisPeriod", "stored"]

    function widthOf(column) { return column.width > 0 ? column.width : descriptionWidth }

    function canEdit(row, key) {
        if (!row) return false
        if (key === "code" || key === "description") return true
        if (key === "scheduled") return !row.locked
        if (sheet.billingKeys.indexOf(key) < 0 || !backend.editable) return false
        return key !== "installed" || row.quantity > 0
    }
    function rowAt(index) {
        return index >= 0 && index < backend.lines.count ? backend.lines.get(index) : null
    }
    function edit(id, key) {
        commit()
        const index = backend.lines.indexOf(id)
        if (index < 0) return
        list.positionViewAtIndex(index, ListView.Contain)
        backend.selectLine(id)
        if (!canEdit(rowAt(index), key)) return
        editKey = key
        editId = id
    }
    // The selected line's most useful cell: its percent complete while the
    // application is a draft, its description otherwise.
    function editSelected() {
        if (!backend.selectedLine) backend.selectRelative(1)
        const id = backend.selectedLine
        if (!id) return
        const row = rowAt(backend.lines.indexOf(id))
        edit(id, canEdit(row, "percent") ? "percent" : "description")
    }
    function commit() {
        if (!editing) return
        const id = editId, key = editKey, text = draft
        editId = 0
        editKey = ""
        if (key === "code" || key === "description" || key === "scheduled") backend.setLineField(id, key, text)
        else if (key === "percent") backend.setPercent(id, text)
        else if (key === "thisPeriod") backend.setThisPeriod(id, text)
        else if (key === "stored") backend.setStored(id, text)
        else if (key === "installed") backend.setInstalled(id, text)
        list.forceActiveFocus()
    }
    function cancel() {
        editId = 0
        editKey = ""
        list.forceActiveFocus()
    }
    // After a commit, move the editor down (or up) a row in the same column,
    // or across the row with Tab.
    function moveEdit(rowStep, columnStep) {
        const id = editId, key = editKey
        commit()
        let index = backend.lines.indexOf(id)
        if (columnStep !== 0) {
            const keys = shown.map(c => c.key)
            let k = keys.indexOf(key)
            for (let tries = 0; tries < keys.length * backend.lines.count; ++tries) {
                k += columnStep
                if (k >= keys.length) { k = 0; index++ }
                if (k < 0) { k = keys.length - 1; index-- }
                if (index < 0 || index >= backend.lines.count) return
                if (canEdit(rowAt(index), keys[k])) { edit(backend.lines.idAt(index), keys[k]); return }
            }
            return
        }
        index += rowStep
        if (index >= 0 && index < backend.lines.count) edit(backend.lines.idAt(index), key)
    }
    function editorText(row, key) {
        const plain = (value, decimals) => {
            let text = value.toFixed(decimals)
            if (decimals > 2 || key === "percent") text = text.replace(/\.?0+$/, "")
            return text.replace(".", Qt.locale().decimalPoint)
        }
        switch (key) {
        case "code": return row.code
        case "description": return row.description
        case "percent": return plain(row.percent, 2)
        case "installed": return plain(row.installed, 3)
        default: return plain(row[key] / 100, 2)
        }
    }

    Shortcut { sequences: ["Space", "Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: win.page === 0 && !win.typing && !win.modal && backend.isOpen; onActivated: sheet.editSelected() }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: win.page === 0 && !win.typing && !win.modal; onActivated: backend.selectRelative(1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: win.page === 0 && !win.typing && !win.modal; onActivated: backend.selectRelative(-1) }
    Shortcut { sequence: "Alt+Down"; context: Qt.ApplicationShortcut; enabled: win.page === 0 && !win.typing && !win.modal && backend.selectedLine > 0; onActivated: backend.moveLine(backend.selectedLine, 1) }
    Shortcut { sequence: "Alt+Up"; context: Qt.ApplicationShortcut; enabled: win.page === 0 && !win.typing && !win.modal && backend.selectedLine > 0; onActivated: backend.moveLine(backend.selectedLine, -1) }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: win.page === 0 && !win.typing && !win.modal && backend.editable && backend.selectedLine > 0; onActivated: backend.completeLine(backend.selectedLine) }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; context: Qt.ApplicationShortcut; enabled: win.page === 0 && !win.typing && !win.modal && backend.selectedLine > 0; onActivated: backend.removeLine(backend.selectedLine) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // What can be done with this application, and the buttons for it.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 8

            Label {
                objectName: "sheetHint"
                Layout.fillWidth: true
                color: win.muted
                wrapMode: Text.Wrap
                text: {
                    const p = backend.period
                    let hint
                    if (!backend.currentPeriod)
                        hint = backend.lines.count === 0
                               ? qsTr("Start with the schedule of values: add its lines, or import a Plumb takeoff.")
                               : qsTr("When the schedule of values is complete, start application 1 to bill it.")
                    else if (backend.editable)
                        hint = qsTr("Enter each line's percent complete to date. Space edits the selected line; Enter saves and moves down.")
                    else
                        hint = qsTr("%1 was submitted, so its amounts are fixed.").arg(win.applicationName(p.number ?? 0))
                    if (backend.sovLocked)
                        hint += " " + qsTr("The original schedule is fixed; add changes as change orders.")
                    return hint
                }
            }
            Button {
                objectName: "addLine"
                visible: !backend.sovLocked
                flat: true
                text: qsTr("Add line")
                onClicked: backend.addLine()
            }
            Button {
                objectName: "startPeriod"
                visible: backend.canStartPeriod && backend.lines.count > 0
                highlighted: true
                text: qsTr("Start %1").arg(win.applicationName(backend.periods.length + 1))
                onClicked: { sheet.commit(); backend.startPeriod() }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: win.line }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: sheet.tableWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.horizontal: ScrollBar {}
            visible: backend.lines.count > 0

            ColumnLayout {
                width: sheet.tableWidth
                height: flick.height
                spacing: 0

                // Column headings.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: win.panel
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: sheet.shown
                            delegate: Label {
                                required property var modelData
                                width: sheet.widthOf(modelData)
                                height: 44
                                leftPadding: 10
                                rightPadding: 10
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: modelData.end ? Text.AlignRight : Text.AlignLeft
                                text: modelData.title
                                color: win.muted
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                            }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }
                }

                ListView {
                    id: list
                    objectName: "lineList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    focus: true
                    model: backend.lines
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: false
                    ScrollBar.vertical: ScrollBar {}
                    currentIndex: backend.lines.indexOf(backend.selectedLine)
                    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

                    delegate: Rectangle {
                        id: rowItem
                        required property int index
                        required property var model
                        readonly property var row: model
                        readonly property bool selected: backend.selectedLine === model.lineId
                        width: sheet.tableWidth
                        height: 40
                        color: selected ? Qt.alpha(theme.accent, 0.12) : hover.hovered ? "#141418" : "transparent"

                        HoverHandler { id: hover }
                        TapHandler {
                            onTapped: { sheet.commit(); backend.selectLine(rowItem.model.lineId) }
                        }
                        Rectangle {
                            visible: rowItem.selected
                            width: 3
                            height: parent.height
                            color: theme.accent
                        }

                        Row {
                            anchors.fill: parent
                            Repeater {
                                model: sheet.shown
                                delegate: SheetCell {
                                    required property var modelData
                                    column: modelData
                                    row: rowItem.row
                                    lineId: rowItem.model.lineId
                                    width: sheet.widthOf(modelData)
                                    height: rowItem.height
                                }
                            }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1c1c21" }
                    }
                }

                // Totals, as on the bottom of the sheet.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: win.panel
                    Rectangle { width: parent.width; height: 1; color: win.line }
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: sheet.shown
                            delegate: Label {
                                required property var modelData
                                readonly property var s: backend.summary
                                width: sheet.widthOf(modelData)
                                height: 44
                                leftPadding: 10
                                rightPadding: 10
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: modelData.end ? Text.AlignRight : Text.AlignLeft
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                text: {
                                    switch (modelData.key) {
                                    case "description": return qsTr("Total")
                                    case "scheduled": return win.money(s.scheduledTotal)
                                    case "previous": return win.money(s.previousTotal)
                                    case "thisPeriod": return win.money(s.thisPeriodTotal)
                                    case "stored": return win.money(s.storedMaterials)
                                    case "toDate": return win.money(s.completedToDate)
                                    case "balance": return win.money(s.balanceTotal)
                                    case "retainage": return win.money(s.retainage)
                                    case "percent": return s.scheduledTotal ? win.percent(100 * s.completedWork / s.scheduledTotal) : ""
                                    default: return ""
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // An empty schedule of values.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: backend.lines.count === 0

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 64, 460)
                spacing: 16
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("No schedule of values yet")
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: win.muted
                    text: qsTr("Type in each line of work with its scheduled value, or import the quantities and prices from a Plumb takeoff.")
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    Button { highlighted: true; text: qsTr("Add line"); onClicked: backend.addLine() }
                    Button { text: qsTr("Import takeoff"); onClicked: backend.importTakeoff() }
                }
            }
        }
    }
}
