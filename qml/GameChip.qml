import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Jogo escolhido nas configurações: ícone + nome (+ detalhe) e um × para remover.
Rectangle {
    id: chip
    property string game: ""
    property string detail: ""
    signal removed()
    implicitHeight: 36
    implicitWidth: row.implicitWidth + 16
    radius: 18
    color: hover.hovered ? Theme.cardHi : Theme.card
    border.color: Theme.line
    HoverHandler { id: hover }

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        x: 5
        spacing: 8
        GameAvatar { game: chip.game; iconUrl: win.gameIcon(chip.game); size: 26; radius: 13 }
        ColumnLayout {
            spacing: 0
            Text { text: chip.game; color: Theme.text; font.pixelSize: 13 }
            Text { visible: chip.detail !== ""; text: chip.detail; color: Theme.faint; font.pixelSize: 10 }
        }
        AbstractButton {
            implicitWidth: 22
            implicitHeight: 22
            hoverEnabled: true
            onClicked: chip.removed()
            ToolTip.visible: hovered
            ToolTip.text: "Remover"
            background: Rectangle { radius: 11; color: parent.hovered ? Qt.rgba(0.96, 0.25, 0.37, 0.18) : "transparent" }
            contentItem: Item { Icon { anchors.centerIn: parent; name: "close"; size: 12; color: parent.parent.hovered ? Theme.kill : Theme.muted } }
        }
    }
}
