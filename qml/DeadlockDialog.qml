import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Tutorial do Deadlock: como pôr -condebug nas opções de inicialização da Steam para o Aeonis gravar
// cada partida do início ao fim (sem a opção, grava a sessão inteira do jogo).
Popup {
    id: d
    readonly property var state: App.deadlock || ({})

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 580
    contentWidth: width - leftPadding - rightPadding // fixo: a largura não depende do conteúdo (sem binding loop)
    height: Math.min(implicitHeight, parent.height - 40) // janela baixa: rola
    modal: true
    padding: 22
    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }
    Overlay.modal: Rectangle { color: "#99000000" }
    onOpened: App.checkDeadlock()

    component Step: RowLayout {
        id: step
        property int number: 1
        property string text: ""
        default property alias extra: more.data
        Layout.fillWidth: true
        spacing: 12
        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 26
            implicitHeight: 26
            radius: 13
            color: Theme.cardHi
            border.color: Theme.line
            Text { anchors.centerIn: parent; text: number; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
        }
        ColumnLayout {
            id: more
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                color: Theme.text
                font.pixelSize: 13
                lineHeight: 1.15
                text: step.text
            }
        }
    }

    contentItem: ScrollView {
        clip: true
        contentWidth: d.contentWidth

    ColumnLayout {
        width: d.contentWidth
        spacing: 16

        RowLayout {
            spacing: 10
            Image {
                source: App.gameIconUrl("Deadlock")
                visible: source.toString() !== ""
                sourceSize: Qt.size(24, 24)
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }
            Text { text: "Gravar cada partida do Deadlock"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true }
            AButton { flat: true; iconName: "close"; onClicked: d.close() }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: 13
            lineHeight: 1.15
            text: "O Deadlock não informa quando a partida começa e termina, a não ser no registro do console. "
                + "Com a opção <b>-condebug</b>, o Aeonis lê esse registro e salva um vídeo por partida, do início ao fim. "
                + "Sem ela, grava a sessão inteira do jogo, inclusive o tempo no saguão e na fila."
            textFormat: Text.StyledText
        }

        Step {
            number: 1
            text: "Na Steam, abra a <b>Biblioteca</b> e encontre o Deadlock."
            AButton {
                iconName: "external"
                text: "Abrir o Deadlock na Steam"
                onClicked: Qt.openUrlExternally("steam://nav/games/details/1422450")
            }
        }
        Step { number: 2; text: "Clique com o botão direito no Deadlock e escolha <b>Propriedades…</b>" }
        Step {
            number: 3
            text: "Na aba <b>Geral</b>, em <b>Opções de inicialização</b>, adicione o texto abaixo. "
                + "Se já houver outras opções, deixe um espaço entre elas."
            RowLayout {
                spacing: 8
                Rectangle {
                    implicitWidth: code.implicitWidth + 24
                    implicitHeight: 34
                    radius: 9
                    color: Theme.bg
                    border.color: Theme.line
                    Text { id: code; anchors.centerIn: parent; text: "-condebug"; color: Theme.text; font.family: "Consolas"; font.pixelSize: 14 }
                }
                AButton { text: "Copiar"; onClicked: App.copyText("-condebug", "-condebug copiado.") }
            }
        }
        Step { number: 4; text: "Feche as propriedades. Vale a partir da próxima vez que o Deadlock abrir (se ele estiver aberto, reinicie o jogo)." }

        // Situação atual
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: statusRow.implicitHeight + 20
            radius: 10
            color: Theme.card
            border.color: d.state.condebug ? Theme.assist : Theme.line
            RowLayout {
                id: statusRow
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                Icon { name: d.state.condebug ? "check" : "info"; size: 18; color: d.state.condebug ? Theme.assist : Theme.multi }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.text
                    font.pixelSize: 13
                    text: d.state.condebug ? "Tudo certo: -condebug está nas opções de inicialização do Deadlock."
                        : d.state.known ? "Ainda não encontrei -condebug nas opções. A Steam pode demorar a salvar a mudança: "
                                          + "se você já adicionou, pode ignorar este aviso."
                        : "Não encontrei as configurações do Deadlock na Steam deste PC."
                }
                AButton { visible: !d.state.condebug; iconName: "refresh"; text: "Verificar"; onClicked: App.checkDeadlock() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AButton {
                visible: !d.state.condebug && !d.state.dismissed
                flat: true
                text: "Não mostrar de novo"
                onClicked: { App.dismissDeadlockTip(); d.close() }
            }
            AButton { primary: true; text: "Fechar"; onClicked: d.close() }
        }
    }
    }
}
