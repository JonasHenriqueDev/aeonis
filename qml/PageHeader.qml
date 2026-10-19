import QtQuick
import QtQuick.Layouts

// Título da página (com o ícone do jogo filtrado) e ações gerais.
RowLayout {
    id: h
    property string title
    property string subtitle
    property bool actions: true
    spacing: 10

    GameAvatar {
        visible: win.gameFilter !== "" && h.actions
        game: win.gameFilter
        iconUrl: win.gameIcon(win.gameFilter)
        size: 40
    }
    ColumnLayout {
        spacing: 2
        Text { text: h.title; color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
        Text { visible: h.subtitle !== ""; text: h.subtitle; color: Theme.muted; font.pixelSize: 13 }
    }
    Item { Layout.fillWidth: true }
    AButton { visible: h.actions; text: "Atualizar"; iconName: "refresh"; onClicked: App.refresh() }
    AButton { visible: h.actions; text: "Abrir pasta"; iconName: "folder"; onClicked: App.openFolder() }
}
