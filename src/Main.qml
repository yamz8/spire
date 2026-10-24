import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1480
    height: 900
    minimumWidth: 1040
    minimumHeight: 600
    visible: true
    title: backend.isOpen ? (backend.info.name ?? "") + " — Spire" : "Spire"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color panel: "#16161a"
    readonly property color raised: "#1d1d22"
    readonly property color line: "#2a2a31"
    readonly property color muted: "#9a9aa3"
    property bool helpVisible: false
    // 0 is the continuation sheet, 1 the change orders.
    property int page: 0
    // Single-key shortcuts stand aside while a text field has the keyboard.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    readonly property bool modal: projectPopup.opened || importPopup.opened || confirm.opened || datePopup.opened

    // Formatting goes through the backend; reading translator.language makes
    // every amount re-render when the language changes.
    function money(cents) { translator.language; backend.info; return backend.money(cents) }
    function percent(value) { translator.language; return backend.percent(value) }
    function date(value) { translator.language; return backend.date(value) }
    function applicationName(number) { return qsTr("Application %1").arg(number.toLocaleString(Qt.locale(), 'f', 0)) }
    // Material's outlined fields leave the gap for a floating label on the
    // left even in right-to-left languages, so there the label only shows
    // while the field is empty and idle.
    function label(field, text) {
        return rtl && (field.activeFocus || field.text.length > 0) ? "" : text
    }
    function showPage(index) {
        sheet.commit()
        page = index
    }

    Shortcut { sequences: ["?", "Shift+?"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.New]; context: Qt.ApplicationShortcut; onActivated: { sheet.commit(); backend.newProject() } }
    Shortcut { sequences: [StandardKey.Open]; context: Qt.ApplicationShortcut; onActivated: { sheet.commit(); backend.openProject() } }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; enabled: backend.isOpen; onActivated: { sheet.commit(); projectPopup.open() } }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; enabled: backend.isOpen && backend.currentPeriod > 0; onActivated: { sheet.commit(); backend.exportPayApplication() } }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.modal && backend.isOpen; onActivated: backend.importTakeoff() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.modal && backend.isOpen; onActivated: win.showPage(0) }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.modal && backend.isOpen; onActivated: win.showPage(1) }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.modal; onActivated: { sheet.commit(); backend.stepPeriod(-1) } }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.modal; onActivated: { sheet.commit(); backend.stepPeriod(1) } }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.modal && backend.canStartPeriod; onActivated: { sheet.commit(); backend.startPeriod() } }
    Shortcut {
        sequence: "A"
        context: Qt.ApplicationShortcut
        enabled: !win.typing && !win.modal && backend.isOpen
        onActivated: win.page === 0 ? backend.addLine() : backend.addChangeOrder()
    }
    Shortcut {
        sequence: "C"
        context: Qt.ApplicationShortcut
        enabled: !win.typing && !win.modal && backend.isOpen
        onActivated: { win.showPage(1); backend.addChangeOrder() }
    }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.modal; onActivated: backend.undo() }
    // Listed by hand: StandardKey.Redo already holds Ctrl+Shift+Z on Linux,
    // and the same key twice makes the shortcut ambiguous, so it never fires.
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.modal; onActivated: backend.redo() }
    // Popups close themselves on Escape, so the window's own Escape stands
    // aside while one is open.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: !win.modal
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (sheet.editing) sheet.cancel()
            else if (win.typing) contentRoot.forceActiveFocus()
            else backend.selectLine(0)
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: contentRoot
        objectName: "root"
        anchors.fill: parent
        focus: true
        LayoutMirroring.enabled: win.rtl
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Top bar: the project, the application on screen, and the PDF.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: win.panel
                visible: backend.isOpen

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 10

                    ToolButton {
                        objectName: "projectButton"
                        contentItem: Label {
                            text: backend.info.name ?? ""
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Layout.maximumWidth: 280
                        onClicked: { sheet.commit(); projectPopup.open() }
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Project details")
                    }
                    Rectangle { width: 1; height: 24; color: win.line }
                    Dropdown {
                        objectName: "periodBox"
                        visible: backend.periods.length > 0
                        Layout.preferredWidth: 330
                        model: backend.periods
                        currentIndex: backend.periods.findIndex(p => p.id === backend.currentPeriod)
                        displayText: currentIndex >= 0 ? periodText(backend.periods[currentIndex]) : ""
                        function periodText(p) {
                            return p ? win.applicationName(p.number) + " · " + qsTr("to %1").arg(win.date(p.periodTo)) : ""
                        }
                        delegate: ItemDelegate {
                            required property var modelData
                            width: ListView.view.width
                            contentItem: RowLayout {
                                spacing: 10
                                Label { Layout.fillWidth: true; text: win.applicationName(modelData.number) + " · " + qsTr("to %1").arg(win.date(modelData.periodTo)) }
                                Label { text: modelData.submitted ? qsTr("Submitted") : qsTr("Draft"); color: modelData.submitted ? win.muted : theme.accent }
                            }
                        }
                        onActivated: (index) => { sheet.commit(); backend.currentPeriod = backend.periods[index].id }
                    }
                    Rectangle {
                        visible: backend.currentPeriod > 0
                        radius: Math.min(4, theme.radius)
                        color: backend.editable ? Qt.alpha(theme.accent, 0.18) : win.raised
                        implicitWidth: statusLabel.implicitWidth + 16
                        implicitHeight: 24
                        Label {
                            id: statusLabel
                            anchors.centerIn: parent
                            text: backend.editable ? qsTr("Draft") : qsTr("Submitted")
                            color: backend.editable ? theme.accent : win.muted
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                    }
                    Item { width: 6 }
                    TabBar {
                        id: tabs
                        objectName: "tabs"
                        currentIndex: win.page
                        background: null
                        TabButton {
                            text: qsTr("Continuation sheet")
                            width: implicitWidth
                            onClicked: win.showPage(0)
                        }
                        TabButton {
                            objectName: "changeOrdersTab"
                            text: backend.pendingChangeOrders > 0
                                  ? qsTr("Change orders (%1 pending)").arg(backend.pendingChangeOrders.toLocaleString(Qt.locale(), 'f', 0))
                                  : qsTr("Change orders")
                            width: implicitWidth
                            onClicked: win.showPage(1)
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        text: qsTr("Import takeoff")
                        onClicked: { sheet.commit(); backend.importTakeoff() }
                    }
                    ToolButton {
                        text: qsTr("Undo")
                        enabled: backend.canUndo
                        onClicked: { sheet.commit(); backend.undo() }
                    }
                    Button {
                        objectName: "exportButton"
                        text: qsTr("Export PDF")
                        highlighted: true
                        enabled: backend.currentPeriod > 0
                        onClicked: { sheet.commit(); backend.exportPayApplication() }
                    }
                    ToolButton {
                        text: "?"
                        font.pixelSize: 16
                        onClicked: win.helpVisible = true
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }
            }

            // Errors are said plainly, in the window.
            Rectangle {
                objectName: "errorBar"
                Layout.fillWidth: true
                Layout.preferredHeight: errorText.implicitHeight + 20
                visible: backend.error !== ""
                color: Qt.alpha(theme.danger, 0.2)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    Label {
                        id: errorText
                        Layout.fillWidth: true
                        text: backend.error
                        wrapMode: Text.Wrap
                    }
                    ToolButton { text: "×"; font.pixelSize: 18; onClicked: backend.clearError() }
                }
            }
            Rectangle {
                objectName: "noticeBar"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                visible: backend.notice !== ""
                color: Qt.alpha(theme.accent, 0.16)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    Label { Layout.fillWidth: true; text: backend.notice; elide: Text.ElideMiddle }
                    ToolButton { text: qsTr("View"); visible: backend.lastExport !== ""; onClicked: backend.openExport() }
                    ToolButton { text: "×"; font.pixelSize: 18; onClicked: backend.clearNotice() }
                }
                Timer { interval: 8000; running: backend.notice !== ""; onTriggered: backend.clearNotice() }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                visible: backend.isOpen

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: win.page

                    SheetView {
                        id: sheet
                        objectName: "sheet"
                    }
                    ChangeOrderView {
                        id: changeOrders
                        objectName: "changeOrders"
                    }
                }
                Rectangle { Layout.fillHeight: true; width: 1; color: win.line }
                SummaryPanel {
                    id: summaryPanel
                    objectName: "summary"
                    Layout.preferredWidth: 340
                    Layout.fillHeight: true
                }
            }

            // No project open.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !backend.isOpen

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 18
                    width: Math.min(parent.width - 64, 480)

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Spire"
                        font.pixelSize: 34
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: win.muted
                        text: qsTr("Progress billing for contractors. Set up the schedule of values, enter each period's percent complete, and export the pay application.")
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 12
                        Button {
                            objectName: "newProject"
                            highlighted: true
                            text: qsTr("New project")
                            onClicked: backend.newProject()
                        }
                        Button {
                            text: qsTr("Open project")
                            onClicked: backend.openProject()
                        }
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        color: win.muted
                        text: qsTr("Press ? for keyboard shortcuts")
                    }
                }
            }
        }

        ProjectPopup { id: projectPopup }
        ImportPopup { id: importPopup }
        ConfirmPopup { id: confirm }
        DatePopup {
            id: datePopup
            property var onChosen: null
            function pick(current, callback) {
                onChosen = callback
                openAt(current)
            }
            onPicked: (date) => { if (onChosen) onChosen(date) }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
        }
    }

    Connections {
        target: backend
        function onImportPreviewChanged() {
            if (backend.importPreview.rows !== undefined) importPopup.open()
            else importPopup.close()
        }
        function onLineAdded(id) { win.page = 0; sheet.edit(id, "description") }
        function onChangeOrderAdded(id) { win.page = 1; changeOrders.focusDescription(id) }
    }
}
