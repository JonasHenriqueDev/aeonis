import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "Format.js" as F

// Janela para escolher jogos: busca na lista de jogos conhecidos, marca/desmarca com um clique e,
// para um jogo fora da lista, "Procurar executável…" abre o Explorer.
Popup {
    id: picker
    property var games: []    // nomes disponíveis
    property var selected: [] // nomes marcados
    property bool allowBrowse: true
    property string title: "Adicionar jogos"
    signal toggled(string game, bool on)
    signal exePicked(var game) // { name, process, icon }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
    height: Math.min(600, (parent ? parent.height : 600) - 80)
    modal: true
    focus: true
    padding: 0
    onOpened: { search.text = ""; search.forceActiveFocus() }

    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.line }

    readonly property var filtered: {
        const q = search.text.trim().toLowerCase()
        const list = games.filter(g => !q || g.toLowerCase().includes(q))
        // Marcados primeiro, depois em ordem alfabética.
        return list.sort((a, b) => (selected.includes(b) - selected.includes(a)) || a.localeCompare(b))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Text { text: picker.title; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text { text: F.plural(picker.selected.length, "jogo escolhido", "jogos escolhidos"); color: Theme.muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            AButton { flat: true; iconName: "close"; onClicked: picker.close() }
        }

        // Busca
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 42
            radius: 10
            color: Theme.card
            border.color: search.activeFocus ? Theme.accent : Theme.line
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 6
                spacing: 8
                Icon { name: "search"; size: 16; color: Theme.muted }
                TextField {
                    id: search
                    Layout.fillWidth: true
                    placeholderText: "Pesquisar jogo…"
                    background: Item {}
                    color: Theme.text
                    font.pixelSize: 14
                    Keys.onReturnPressed: if (picker.filtered.length === 1) picker.toggled(picker.filtered[0], !picker.selected.includes(picker.filtered[0]))
                }
                AButton { visible: search.text !== ""; flat: true; iconName: "close"; onClicked: search.text = "" }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: picker.filtered
            ScrollBar.vertical: ScrollBar {}
            FastWheel {}
            delegate: AbstractButton {
                id: item
                required property string modelData
                readonly property bool on: picker.selected.includes(modelData)
                width: ListView.view.width
                height: 48
                hoverEnabled: true
                onClicked: picker.toggled(modelData, !on)
                background: Rectangle { radius: 10; color: item.hovered ? Theme.card : "transparent" }
                contentItem: RowLayout {
                    spacing: 12
                    GameAvatar { Layout.leftMargin: 8; game: item.modelData; iconUrl: win.gameIcon(item.modelData); size: 32 }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: item.modelData
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: item.on ? Font.DemiBold : Font.Normal
                    }
                    // Marcado: círculo com ✓; senão, um + discreto.
                    Rectangle {
                        Layout.rightMargin: 8
                        width: 26; height: 26; radius: 13
                        color: item.on ? Theme.accent : "transparent"
                        border.color: item.on ? Theme.accent : (item.hovered ? Theme.muted : Theme.line)
                        Icon { anchors.centerIn: parent; name: item.on ? "check" : "plus"; size: 14; color: item.on ? "white" : Theme.muted }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: picker.filtered.length === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: "Nenhum jogo com \"" + search.text + "\" na lista.\nProcure o executável do jogo abaixo."
                color: Theme.muted
                font.pixelSize: 13
            }
        }

        Rectangle { visible: picker.allowBrowse; Layout.fillWidth: true; height: 1; color: Theme.line }
        RowLayout {
            visible: picker.allowBrowse
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                spacing: 1
                Text { text: "Não está na lista?"; color: Theme.text; font.pixelSize: 13 }
                Text { text: "Escolha o .exe do jogo no Explorer."; color: Theme.faint; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            AButton { text: "Procurar executável…"; iconName: "folder"; primary: true; onClicked: exeDialog.open() }
        }
    }

    FileDialog {
        id: exeDialog
        title: "Escolha o executável do jogo"
        nameFilters: ["Executáveis (*.exe)"]
        currentFolder: "file:///C:/Program Files"
        onAccepted: {
            const g = App.gameFromExe(selectedFile)
            if (g && g.name) picker.exePicked(g)
        }
    }
}
