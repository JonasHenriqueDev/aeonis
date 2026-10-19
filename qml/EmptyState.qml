import QtQuick
import QtQuick.Layouts

ColumnLayout {
    spacing: 12
    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        width: 64; height: 64; radius: 18
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.gradA }
            GradientStop { position: 1; color: Theme.gradB }
        }
        Icon { anchors.centerIn: parent; name: "bolt"; size: 30; color: "white" }
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        text: App.library.matches === undefined ? "Carregando…" : "Nenhum clipe ainda"
        color: Theme.text
        font.pixelSize: 20
        font.weight: Font.DemiBold
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 460
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.muted
        font.pixelSize: 14
        textFormat: Text.StyledText
        text: "Abra um jogo: o Aeonis começa a gravar em segundo plano sozinho.<br>Pressione <b>"
              + (App.status.hotkeySave || "Alt+F10")
              + "</b> para salvar os últimos segundos, ou jogue LoL/CS2 para receber destaques automáticos."
    }
}
