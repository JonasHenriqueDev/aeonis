import QtQuick
import QtQuick.Layouts

// Título da página (com o ícone do jogo filtrado), um controle extra opcional e ações gerais.
RowLayout {
    id: h
    property string title
    property string subtitle
    property bool actions: true
    property string game: ""          // jogo filtrado (mostra o ícone)
    property Component extra: null    // ex.: seletor de jogo
    spacing: 10

    GameAvatar {
        visible: h.game !== "" && h.actions
        game: h.game
        iconUrl: win.gameIcon(h.game)
        size: 40
    }
    ColumnLayout {
        spacing: 2
        Text { text: h.title; color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
        Text { visible: h.subtitle !== ""; text: h.subtitle; color: Theme.muted; font.pixelSize: 13 }
    }
    Item { Layout.fillWidth: true }
    Loader { active: h.extra !== null; sourceComponent: h.extra }
    AButton { visible: h.actions; text: "Atualizar"; iconName: "refresh"; onClicked: App.refresh() }
    AButton { visible: h.actions; text: "Abrir pasta"; iconName: "folder"; onClicked: App.openFolder() }
}
