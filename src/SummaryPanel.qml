import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The application and certificate for payment (G702) for the application on
// screen: its dates and retainage, the nine lines down to the payment due,
// and what can be done with it next.
Rectangle {
    id: panel
    color: win.panel

    readonly property var s: backend.summary
    readonly property var p: backend.period
    readonly property bool hasPeriod: backend.currentPeriod > 0

    function commitRetainage() {
        if (!backend.editable) return
        if (workField.text !== workField.shown) backend.setPeriodField("retainageWork", workField.text)
        if (storedField.text !== storedField.shown) backend.setPeriodField("retainageStored", storedField.text)
        // A value that was refused goes back to what is saved.
        workField.text = Qt.binding(() => workField.shown)
        storedField.text = Qt.binding(() => storedField.shown)
    }
    function percentText(value) {
        return (value ?? 0).toLocaleString(Qt.locale(), 'f', 2).replace(new RegExp("\\" + Qt.locale().decimalPoint + "?0+$"), "")
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: panel.hasPeriod ? win.applicationName(panel.p.number ?? 0) : qsTr("Before the first application")
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }

                // Dates and retainage of this application.
                GridLayout {
                    Layout.fillWidth: true
                    visible: panel.hasPeriod
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 6

                    Button {
                        objectName: "periodTo"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        flat: true
                        enabled: backend.editable
                        contentItem: ColumnLayout {
                            spacing: 0
                            Label { text: qsTr("Period to"); color: win.muted; font.pixelSize: 11 }
                            Label { text: win.date(panel.p.periodTo); font.pixelSize: 14 }
                        }
                        onClicked: datePopup.pick(panel.p.periodTo, (d) => backend.setPeriodField("periodTo", d))
                    }
                    Button {
                        objectName: "applicationDate"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        flat: true
                        enabled: backend.editable
                        contentItem: ColumnLayout {
                            spacing: 0
                            Label { text: qsTr("Application date"); color: win.muted; font.pixelSize: 11 }
                            Label { text: win.date(panel.p.applicationDate); font.pixelSize: 14 }
                        }
                        onClicked: datePopup.pick(panel.p.applicationDate, (d) => backend.setPeriodField("applicationDate", d))
                    }
                    Label {
                        Layout.columnSpan: 2
                        Layout.topMargin: 4
                        text: qsTr("Retainage")
                        color: win.muted
                        font.pixelSize: 11
                    }
                    TextField {
                        id: workField
                        objectName: "retainageWork"
                        readonly property string shown: panel.percentText(panel.p.retainageWork)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        enabled: backend.editable
                        placeholderText: win.label(workField, qsTr("On work, %"))
                        text: shown
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onEditingFinished: panel.commitRetainage()
                    }
                    TextField {
                        id: storedField
                        objectName: "retainageStored"
                        readonly property string shown: panel.percentText(panel.p.retainageStored)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        enabled: backend.editable
                        placeholderText: win.label(storedField, qsTr("On stored, %"))
                        text: shown
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onEditingFinished: panel.commitRetainage()
                    }
                }

                // The nine lines of the certificate.
                Repeater {
                    model: [
                        { n: "1", label: qsTr("Original contract sum"), value: panel.s.originalContract },
                        { n: "2", label: qsTr("Net change by change orders"), value: panel.s.changeOrders },
                        { n: "3", label: qsTr("Contract sum to date"), value: panel.s.contractToDate, strong: true },
                        { n: "4", label: qsTr("Completed and stored to date"), value: panel.s.completedToDate },
                        { n: "5", label: qsTr("Retainage"), value: panel.s.retainage,
                          detail: qsTr("%1 on work, %2 on stored").arg(win.money(panel.s.retainageWork)).arg(win.money(panel.s.retainageStored)) },
                        { n: "6", label: qsTr("Earned less retainage"), value: panel.s.earnedLessRetainage },
                        { n: "7", label: qsTr("Less previous certificates"), value: panel.s.previousCertificates },
                        { n: "9", label: qsTr("Balance to finish"), value: panel.s.balanceToFinish, detail: qsTr("Including retainage") },
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 1
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label { text: modelData.n; color: win.muted; Layout.preferredWidth: 14 }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.label
                                elide: Text.ElideRight
                                font.weight: modelData.strong ? Font.DemiBold : Font.Normal
                            }
                            Label {
                                text: win.money(modelData.value ?? 0)
                                font.weight: modelData.strong ? Font.DemiBold : Font.Normal
                            }
                        }
                        Label {
                            visible: modelData.detail !== undefined
                            Layout.fillWidth: true
                            Layout.leftMargin: 22
                            text: modelData.detail ?? ""
                            color: win.muted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }

                // Line 8, what this application asks to be paid.
                Rectangle {
                    objectName: "currentDue"
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    implicitHeight: dueColumn.implicitHeight + 24
                    radius: theme.radius
                    color: Qt.alpha(theme.accent, 0.14)
                    border.color: Qt.alpha(theme.accent, 0.5)
                    ColumnLayout {
                        id: dueColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 2
                        Label { text: "8  " + qsTr("Current payment due"); color: win.muted }
                        Label {
                            objectName: "currentDueAmount"
                            text: win.money(panel.s.currentDue ?? 0)
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            color: (panel.s.currentDue ?? 0) < 0 ? theme.danger : theme.accent
                        }
                        Label {
                            text: qsTr("%1 complete").arg(win.percent(panel.s.percentComplete ?? 0))
                            color: win.muted
                        }
                    }
                }

                // The change order summary.
                Label {
                    Layout.topMargin: 6
                    text: qsTr("Change orders")
                    font.weight: Font.DemiBold
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 10
                    rowSpacing: 4
                    Label { Layout.fillWidth: true; text: "" }
                    Label { text: qsTr("Additions"); color: win.muted; font.pixelSize: 12; Layout.alignment: Qt.AlignRight }
                    Label { text: qsTr("Deductions"); color: win.muted; font.pixelSize: 12; Layout.alignment: Qt.AlignRight }
                    Label { Layout.fillWidth: true; text: qsTr("Previous months"); elide: Text.ElideRight }
                    Label { text: win.money(panel.s.coPreviousAdditions ?? 0); Layout.alignment: Qt.AlignRight }
                    Label { text: win.money(-(panel.s.coPreviousDeductions ?? 0)); Layout.alignment: Qt.AlignRight }
                    Label { Layout.fillWidth: true; text: qsTr("This month"); elide: Text.ElideRight }
                    Label { text: win.money(panel.s.coThisAdditions ?? 0); Layout.alignment: Qt.AlignRight }
                    Label { text: win.money(-(panel.s.coThisDeductions ?? 0)); Layout.alignment: Qt.AlignRight }
                }

                // What to do next with this application.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    spacing: 6

                    Button {
                        objectName: "submitPeriod"
                        Layout.fillWidth: true
                        visible: backend.editable
                        highlighted: true
                        text: qsTr("Submit application")
                        onClicked: {
                            sheet.commit()
                            confirm.ask(qsTr("Submit %1?").arg(win.applicationName(panel.p.number)),
                                        qsTr("Its amounts are fixed once it is submitted, and the original schedule of values is locked. You can reopen the latest application if something needs to change."),
                                        qsTr("Submit"), false, () => backend.submitPeriod())
                        }
                    }
                    Button {
                        objectName: "startNext"
                        Layout.fillWidth: true
                        visible: backend.canStartPeriod && backend.lines.count > 0
                        highlighted: !backend.editable
                        text: qsTr("Start %1").arg(win.applicationName(backend.periods.length + 1))
                        onClicked: backend.startPeriod()
                    }
                    Button {
                        Layout.fillWidth: true
                        visible: panel.p.canReopen ?? false
                        flat: true
                        text: qsTr("Reopen to change it")
                        onClicked: backend.reopenPeriod()
                    }
                    Button {
                        objectName: "deletePeriod"
                        Layout.fillWidth: true
                        visible: panel.p.canRemove ?? false
                        flat: true
                        text: qsTr("Delete this draft")
                        onClicked: confirm.ask(qsTr("Delete %1?").arg(win.applicationName(panel.p.number)),
                                               qsTr("Its billing is deleted with it. Ctrl+Z brings it back."),
                                               qsTr("Delete"), true, () => backend.removePeriod())
                    }
                }
            }
        }
    }
}
