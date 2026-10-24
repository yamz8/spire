import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Material's ComboBox pads its text on the left only, so in a right-to-left
// language the text runs into the edge; this pads both sides.
ComboBox {
    id: box
    Binding {
        target: box.contentItem
        property: "rightPadding"
        value: box.Material.textFieldHorizontalPadding
    }
}
