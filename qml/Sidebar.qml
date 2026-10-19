import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Coluna estreita de ícones (como no Outplayed): Sessões, Clipes, Configurações, Sobre e o status da
// captura embaixo. O jogo e o dia são escolhidos dentro da tela de Sessões.
Rectangle {
    color: Theme.side
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

    component RailButton: AbstractButton {
        id: rb
        property string iconName
        property bool on: false
        Layout.fillWidth: true
        implicitHeight: 62
        topPadding: 10
        bottomPadding: 10
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.text: text
        ToolTip.delay: 500
        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.margins: 6
                radius: 10
                color: rb.on ? Theme.cardHi : rb.hovered ? Theme.card : "transparent"
            }
            Rectangle { // marca da aba ativa
                visible: rb.on
                x: 0
                anchors.verticalCenter: parent.verticalCenter
                width: 3; height: 24; radius: 1.5
                color: Theme.accent
            }
        }
        contentItem: ColumnLayout {
            spacing: 3
            Icon { Layout.alignment: Qt.AlignHCenter; name: rb.iconName; size: 21; color: rb.on ? Theme.text : Theme.muted }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: rb.text
                color: rb.on ? Theme.text : Theme.muted
                font.pixelSize: 10
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 10
        spacing: 2

        RailButton { text: "Sessões"; iconName: "play"; on: win.view === "sessions" && !win.current; onClicked: win.go("sessions") }
        RailButton { text: "Clipes"; iconName: "film"; on: win.view === "clips" && !win.current; onClicked: win.go("clips") }
        Item { Layout.fillHeight: true }
        RailButton { text: "Ajustes"; iconName: "gear"; on: win.view === "settings" && !win.current; onClicked: win.go("settings") }
        RailButton { text: "Sobre"; iconName: "info"; on: win.view === "about" && !win.current; onClicked: win.go("about") }

        // Status da captura: ponto (gravando/buffer/aguardando) com os detalhes na dica.
        Item {
            Layout.fillWidth: true
            implicitHeight: 34
            Rectangle {
                anchors.centerIn: parent
                width: 12; height: 12; radius: 6
                color: App.status.state === "recording" ? Theme.kill : App.status.state === "buffer" ? Theme.assist : Theme.faint
                SequentialAnimation on opacity {
                    running: App.status.state === "recording"
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 700 }
                    NumberAnimation { to: 1; duration: 700 }
                }
            }
            HoverHandler { id: statusHover }
            ToolTip.visible: statusHover.hovered
            ToolTip.text: (App.status.state === "recording" ? "Gravando · " + F.gameName(App.status.game)
                         : App.status.state === "buffer" ? "Buffer ativo · " + F.gameName(App.status.game) : "Aguardando jogo")
                          + "\n" + (App.status.state === "idle" ? App.status.hotkeySave + " salva clipe · v" + App.status.version
                                                                : App.status.encoder + " · " + App.status.hotkeySave + " salva clipe")
        }
    }
}
