import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A month to pick a date from, in the language's own week and names.
Popup {
    id: popup
    signal picked(date date)

    property int month: new Date().getMonth()
    property int year: new Date().getFullYear()
    property var current: new Date(NaN)

    function openAt(date) {
        current = date && !isNaN(date) ? date : new Date(NaN)
        const shown = isNaN(current) ? new Date() : current
        month = shown.getMonth()
        year = shown.getFullYear()
        open()
    }
    function step(delta) {
        const next = new Date(year, month + delta, 1)
        month = next.getMonth()
        year = next.getFullYear()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 16
    background: Rectangle { color: win.panel; radius: theme.radius; border.color: win.line }

    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            ToolButton { text: "‹"; font.pixelSize: 20; onClicked: popup.step(-1) }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                font.weight: Font.Medium
                text: { translator.language; return new Date(popup.year, popup.month, 1).toLocaleDateString(Qt.locale(), "MMMM yyyy") }
            }
            ToolButton { text: "›"; font.pixelSize: 20; onClicked: popup.step(1) }
        }
        DayOfWeekRow {
            Layout.fillWidth: true
            locale: { translator.language; return Qt.locale() }
        }
        MonthGrid {
            id: grid
            Layout.fillWidth: true
            Layout.preferredWidth: 300
            month: popup.month
            year: popup.year
            locale: { translator.language; return Qt.locale() }
            delegate: Rectangle {
                required property var model
                readonly property bool chosen: !isNaN(popup.current) && model.date.toDateString() === popup.current.toDateString()
                implicitWidth: 40
                implicitHeight: 34
                radius: Math.min(4, theme.radius)
                color: chosen ? theme.accent : model.today ? "#26262d" : "transparent"
                opacity: model.month === popup.month ? 1 : 0.35
                Label {
                    anchors.centerIn: parent
                    text: model.day.toLocaleString(Qt.locale(), 'f', 0)
                    color: parent.chosen ? theme.accentForeground : Material.foreground
                }
            }
            onClicked: (date) => { popup.picked(date); popup.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "today"
                flat: true
                text: qsTr("Today")
                onClicked: { popup.picked(new Date()); popup.close() }
            }
            Item { Layout.fillWidth: true }
            Button { flat: true; text: qsTr("Cancel"); onClicked: popup.close() }
        }
    }
}
