import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Botão compacto com ícone, no visual da biblioteca.
AbstractButton {
    id: b
    property string iconName: ""
    property bool primary: false
    property bool danger: false
    property bool flat: false
    property color iconColor: danger ? Theme.kill : Theme.text
    implicitHeight: 34
    implicitWidth: row.implicitWidth + (text !== "" ? 26 : 18)
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: 9
        color: b.primary ? (b.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
             : b.flat ? (b.hovered ? Theme.cardHi : "transparent")
             : (b.hovered ? Theme.cardHi : Theme.card)
        border.color: b.primary || b.flat ? "transparent" : Theme.line
        opacity: b.enabled ? 1 : 0.45
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 7
            Icon { visible: b.iconName !== ""; name: b.iconName; size: 16; color: b.iconColor }
            Text {
                visible: b.text !== ""
                text: b.text
                color: b.danger ? Theme.kill : Theme.text
                font.pixelSize: 13
                font.family: Theme.font
            }
        }
    }
}
