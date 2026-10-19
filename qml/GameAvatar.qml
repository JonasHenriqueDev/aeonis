import QtQuick
import "Format.js" as F

// Ícone do jogo (extraído do executável) ou iniciais num degradê com a cor do nome.
Rectangle {
    id: a
    property string game: ""
    property string iconUrl: ""
    property int size: 28
    width: size
    height: size
    radius: size * 0.28
    readonly property int h: F.hue(game)
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: a.iconUrl !== "" ? Theme.card : Qt.hsla(a.h / 360, 0.6, 0.45, 1) }
        GradientStop { position: 1; color: a.iconUrl !== "" ? Theme.card : Qt.hsla(((a.h + 40) % 360) / 360, 0.65, 0.35, 1) }
    }
    Image {
        anchors.fill: parent
        anchors.margins: a.size * 0.08
        visible: a.iconUrl !== ""
        source: a.iconUrl
        sourceSize: Qt.size(a.size * 2, a.size * 2)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }
    Text {
        anchors.centerIn: parent
        visible: a.iconUrl === ""
        text: F.initials(a.game)
        color: "white"
        font.pixelSize: a.size * 0.38
        font.bold: true
    }
}
