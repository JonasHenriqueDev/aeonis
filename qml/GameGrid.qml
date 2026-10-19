import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Escolha do jogo na tela de Sessões: grade de jogos com ícone, número de sessões e o último dia.
Popup {
    id: grid
    property var games: []   // [{ name, count, last }]
    property string current: ""
    signal chosen(string name)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(860, (parent ? parent.width : 900) - 120)
    height: Math.min(560, (parent ? parent.height : 600) - 100)
    modal: true
    focus: true
    padding: 0
    onOpened: search.text = ""

    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.line }

    readonly property var filtered: games.filter(g => !search.text || F.gameName(g.name).toLowerCase().includes(search.text.toLowerCase()))

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                spacing: 2
                Text { text: "Sessões"; color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                Text { text: "Escolha o jogo"; color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: 240
                implicitHeight: 36
                radius: 9
                color: Theme.card
                border.color: search.activeFocus ? Theme.accent : Theme.line
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    spacing: 6
                    Icon { name: "search"; size: 15; color: Theme.muted }
                    TextField {
                        id: search
                        Layout.fillWidth: true
                        placeholderText: "Pesquisar"
                        background: Item {}
                        color: Theme.text
                        font.pixelSize: 13
                    }
                }
            }
            AButton { flat: true; iconName: "close"; onClicked: grid.close() }
        }

        GridView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property int columns: Math.max(2, Math.floor(width / 190))
            cellWidth: Math.floor(width / columns)
            cellHeight: 176
            model: grid.filtered
            ScrollBar.vertical: ScrollBar {}
            FastWheel {}
            delegate: Item {
                id: cell
                required property var modelData
                width: view.cellWidth
                height: view.cellHeight
                AbstractButton {
                    id: tile
                    anchors.fill: parent
                    anchors.margins: 6
                    hoverEnabled: true
                    readonly property bool on: cell.modelData.name === grid.current
                    onClicked: { grid.chosen(cell.modelData.name); grid.close() }
                    background: Rectangle {
                        radius: 14
                        color: tile.hovered ? Theme.cardHi : Theme.card
                        border.color: tile.on ? Theme.accent : tile.hovered ? Theme.muted : Theme.line
                        border.width: tile.on ? 2 : 1
                        // Brilho com a cor do jogo atrás do ícone.
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 18
                            width: 90; height: 90; radius: 45
                            color: Qt.hsla(F.hue(cell.modelData.name) / 360, 0.6, 0.45, tile.hovered ? 0.22 : 0.12)
                        }
                    }
                    contentItem: ColumnLayout {
                        spacing: 4
                        Item { Layout.preferredHeight: 10 }
                        GameAvatar {
                            Layout.alignment: Qt.AlignHCenter
                            game: cell.modelData.name
                            iconUrl: win.gameIcon(cell.modelData.name)
                            size: 64
                        }
                        Item { Layout.preferredHeight: 8 }
                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: F.gameName(cell.modelData.name)
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: F.plural(cell.modelData.count, "sessão", "sessões") + " · " + F.dayLabel(cell.modelData.last)
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }
}
