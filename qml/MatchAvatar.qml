import QtQuick
import QtQuick.Effects
import "Format.js" as F

// Avatar de uma partida: ícone do campeão (LoL), recortado em círculo, ou o ícone do jogo.
Item {
    id: av
    property var match
    property int size: 40
    readonly property var info: match && match.info ? match.info : ({})
    readonly property bool hasChampion: info.champion !== undefined
    implicitWidth: size
    implicitHeight: size

    Rectangle {
        anchors.fill: parent
        visible: av.hasChampion
        radius: width / 2
        color: Theme.card
        border.color: Theme.line
        Text {
            anchors.centerIn: parent
            visible: champ.status !== Image.Ready
            text: (av.info.champion || "?").slice(0, 2).toUpperCase()
            color: Theme.text
            font.pixelSize: av.size * 0.36
            font.weight: Font.DemiBold
        }
    }
    Image {
        id: champ
        anchors.fill: parent
        visible: false
        source: av.hasChampion ? av.info.championIcon || "" : ""
        fillMode: Image.PreserveAspectCrop
        // Os ícones do Data Dragon têm uma moldura escura: corta um pouco das bordas.
        sourceClipRect: Qt.rect(6, 6, 108, 108)
    }
    Rectangle {
        id: circle
        anchors.fill: parent
        radius: width / 2
        visible: false
        layer.enabled: true
    }
    MultiEffect {
        anchors.fill: parent
        visible: av.hasChampion && champ.status === Image.Ready
        source: champ
        maskEnabled: true
        maskSource: circle
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }
    GameAvatar {
        anchors.centerIn: parent
        visible: !av.hasChampion
        game: av.match ? av.match.game : ""
        iconUrl: av.match ? win.gameIcon(av.match.game) : ""
        size: av.size
    }
}
