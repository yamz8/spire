import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Change orders: each one waits as pending until it is approved, and from
// the application it was approved on it is a line of the continuation sheet
// like any other.
Item {
    id: view

    function focusDescription(id) {
        Qt.callLater(() => {
            const index = backend.changeOrders.findIndex(c => c.id === id)
            if (index < 0) return
            list.positionViewAtIndex(index, ListView.Contain)
            const item = list.itemAtIndex(index)
            if (item) item.focusDescription()
        })
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 8
            Label {
                Layout.fillWidth: true
                color: win.muted
                wrapMode: Text.Wrap
                text: qsTr("Approving a change order adds it to the contract on the application being worked on, where it can be billed. Negative amounts are deductions.")
            }
            Button {
                objectName: "addChangeOrder"
                highlighted: true
                text: qsTr("Add change order")
                onClicked: backend.addChangeOrder()
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: win.line }

        // Column headings.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 12
            visible: list.count > 0
            Label { text: qsTr("No."); color: win.muted; font.pixelSize: 12; Layout.preferredWidth: 56 }
            Label { text: qsTr("Description"); color: win.muted; font.pixelSize: 12; Layout.fillWidth: true }
            Label { text: qsTr("Date"); color: win.muted; font.pixelSize: 12; Layout.preferredWidth: 120; horizontalAlignment: Text.AlignHCenter }
            Label { text: qsTr("Amount"); color: win.muted; font.pixelSize: 12; Layout.preferredWidth: 150; horizontalAlignment: Text.AlignRight }
            Label { text: qsTr("Status"); color: win.muted; font.pixelSize: 12; Layout.preferredWidth: 250 }
            Item { Layout.preferredWidth: 40 }
        }

        ListView {
            id: list
            objectName: "changeOrderList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // A count, not the list itself, so rows (and the field being
            // typed in) survive changes to the other rows.
            model: backend.changeOrders.length
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: rowItem
                required property int index
                readonly property var co: backend.changeOrders[index] ?? ({})
                width: list.width
                height: 72
                color: index % 2 ? "transparent" : "#121215"

                function focusDescription() {
                    descriptionField.forceActiveFocus()
                    descriptionField.selectAll()
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12

                    Label {
                        Layout.preferredWidth: 56
                        Layout.topMargin: 6
                        text: qsTr("CO %1").arg((rowItem.co.number ?? 0).toLocaleString(Qt.locale(), 'f', 0))
                        color: theme.accent
                        font.weight: Font.DemiBold
                    }
                    TextField {
                        id: descriptionField
                        objectName: "coDescription"
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        placeholderText: win.label(descriptionField, qsTr("What changed"))
                        text: rowItem.co.description ?? ""
                        onEditingFinished: {
                            if (text.trim() !== (rowItem.co.description ?? ""))
                                backend.setChangeOrderField(rowItem.co.id, "description", text)
                            text = Qt.binding(() => rowItem.co.description ?? "")
                        }
                    }
                    Button {
                        Layout.preferredWidth: 120
                        Layout.topMargin: 6
                        flat: true
                        text: rowItem.co.date ? win.date(rowItem.co.date) : qsTr("Set date")
                        onClicked: datePopup.pick(rowItem.co.date, (d) => backend.setChangeOrderField(rowItem.co.id, "date", d))
                    }
                    TextField {
                        id: amountField
                        objectName: "coAmount"
                        readonly property string shown: ((rowItem.co.amount ?? 0) / 100).toFixed(2).replace(".", Qt.locale().decimalPoint)
                        Layout.preferredWidth: 150
                        Layout.topMargin: 6
                        horizontalAlignment: Text.AlignRight
                        enabled: !(rowItem.co.locked ?? false)
                        placeholderText: win.label(amountField, qsTr("Amount"))
                        text: activeFocus ? shown : win.money(rowItem.co.amount ?? 0)
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onActiveFocusChanged: if (activeFocus) selectAll()
                        onEditingFinished: {
                            if (text !== shown && text !== win.money(rowItem.co.amount ?? 0))
                                backend.setChangeOrderField(rowItem.co.id, "amount", text)
                            text = Qt.binding(() => activeFocus ? shown : win.money(rowItem.co.amount ?? 0))
                        }
                    }
                    RowLayout {
                        Layout.preferredWidth: 250
                        Layout.fillWidth: false
                        spacing: 6
                        Switch {
                            objectName: "coApproved"
                            checked: rowItem.co.approved ?? false
                            enabled: !(rowItem.co.approved ?? false) || (rowItem.co.canUnapprove ?? false)
                            onToggled: {
                                backend.setApproved(rowItem.co.id, checked)
                                checked = Qt.binding(() => rowItem.co.approved ?? false)
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: rowItem.co.approved ? qsTr("Approved on %1").arg(win.applicationName(rowItem.co.approvedIn)) : qsTr("Pending")
                            color: rowItem.co.approved ? Material.foreground : theme.warning
                        }
                    }
                    ToolButton {
                        Layout.preferredWidth: 40
                        text: "×"
                        font.pixelSize: 18
                        enabled: rowItem.co.canRemove ?? false
                        onClicked: backend.removeLine(rowItem.co.id)
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Delete change order")
                    }
                }
            }
        }

        // No change orders yet.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: list.count === 0
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 64, 440)
                spacing: 14
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("No change orders")
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: win.muted
                    text: qsTr("Added work and deductions go here. Press C to add one.")
                }
            }
        }
    }
}
