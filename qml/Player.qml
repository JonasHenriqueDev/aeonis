import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "Format.js" as F

// Player: vídeo (Qt Multimedia/FFmpeg: H.264 e HEVC sem codec da Store), linha do tempo com os
// eventos da partida, recorte sem re-encode e os outros vídeos da mesma partida ao lado.
FocusScope {
    id: p
    property var media
    readonly property var match: media ? win.matchOf(media) : null
    property var trim: null // { a, b } em segundos
    property bool trimming: false
    readonly property real duration: player.duration > 0 ? player.duration / 1000 : (media ? Math.max(0, win.durationOf(media)) : 0)
    readonly property real position: player.position / 1000
    focus: true
    Component.onCompleted: {
        forceActiveFocus()
        if (media)
            player.source = media.url
    }

    onMediaChanged: {
        trim = null
        if (media && player.source.toString() !== media.url)
            player.source = media.url
    }

    function seek(t) { player.position = Math.max(0, Math.min(duration, t)) * 1000 }
    function setMark(which) {
        if (!trim) return
        const t = Object.assign({}, trim)
        t[which] = position
        if (t.a > t.b) { const x = t.a; t.a = t.b; t.b = x }
        trim = t
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) { if (trim) trim = null; else win.current = null }
        else if (event.key === Qt.Key_Space) { player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play() }
        else if (event.key === Qt.Key_Right) seek(position + 5)
        else if (event.key === Qt.Key_Left) seek(position - 5)
        else if (event.key === Qt.Key_Period) { player.pause(); seek(position + 1 / 60) }
        else if (event.key === Qt.Key_Comma) { player.pause(); seek(position - 1 / 60) }
        else if (event.key === Qt.Key_Delete) win.confirmDelete(media)
        else return
        event.accepted = true
    }

    MediaPlayer {
        id: player
        videoOutput: video
        audioOutput: AudioOutput { id: audio; volume: volumeSlider.value; muted: muteButton.muted }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) play()
    }

    Connections {
        target: App
        function onTrimDone(ok, path) { p.trimming = false; if (ok) p.trim = null }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // Barra superior
        RowLayout {
            spacing: 10
            AButton { text: "Voltar"; iconName: "back"; onClicked: win.current = null }
            GameAvatar { game: p.media ? p.media.game : ""; iconUrl: p.media ? win.gameIcon(p.media.game) : ""; size: 36 }
            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: p.media ? F.chipText(p.media) + " · " + F.gameName(p.media.game) : ""
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Text {
                    text: p.media ? F.dayLabel(p.media.created) + ", " + F.time(p.media.created) + " · " + F.size(p.media.size) : ""
                    color: Theme.muted
                    font.pixelSize: 12
                }
            }
            AButton {
                text: p.media && p.media.saved ? "Salvo" : "Salvar"
                iconName: p.media && p.media.saved ? "starFill" : "star"
                iconColor: p.media && p.media.saved ? Theme.star : Theme.text
                ToolTip.visible: hovered
                ToolTip.text: "Vídeos salvos nunca são apagados pelo limite de armazenamento."
                onClicked: App.setSaved([p.media.path], !p.media.saved)
            }
            AButton { text: "Cortar"; iconName: "scissors"; enabled: p.duration > 0; onClicked: p.trim = { a: 0, b: p.duration } }
            AButton { text: "Mostrar na pasta"; iconName: "folder"; onClicked: App.reveal(p.media.path) }
            AButton { text: "Abrir fora"; iconName: "external"; onClicked: App.openExternal(p.media.path) }
            AButton { text: "Excluir"; iconName: "trash"; danger: true; onClicked: win.confirmDelete(p.media) }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radius
                    color: "black"
                    clip: true
                    VideoOutput {
                        id: video
                        anchors.fill: parent
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                        onDoubleClicked: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen()
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 12
                        visible: player.error !== MediaPlayer.NoError
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Não foi possível reproduzir este vídeo aqui."
                            color: Theme.text
                        }
                        AButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Abrir no player do Windows"
                            iconName: "external"
                            onClicked: App.openExternal(p.media.path)
                        }
                    }
                }

                // Linha do tempo com marcadores de eventos e a faixa do corte
                Item {
                    id: timeline
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    Rectangle {
                        id: track
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 6
                        radius: 3
                        color: Theme.cardHi
                    }
                    Rectangle {
                        visible: p.trim !== null && p.duration > 0
                        anchors.verticalCenter: parent.verticalCenter
                        x: p.trim ? p.trim.a / p.duration * parent.width : 0
                        width: p.trim ? (p.trim.b - p.trim.a) / p.duration * parent.width : 0
                        height: 14
                        radius: 4
                        color: Qt.rgba(0.545, 0.36, 0.965, 0.35)
                        border.color: Theme.accent
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: p.duration > 0 ? p.position / p.duration * parent.width : 0
                        height: 6
                        radius: 3
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: Theme.gradA }
                            GradientStop { position: 1; color: Theme.gradB }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: mouse => p.seek(mouse.x / width * p.duration)
                        onPositionChanged: mouse => { if (pressed) p.seek(mouse.x / width * p.duration) }
                    }
                    Repeater {
                        model: p.media && p.duration > 0 ? p.media.markers : []
                        delegate: Rectangle {
                            required property var modelData
                            x: modelData.t / p.duration * timeline.width - 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 12; height: 12; radius: 6
                            color: Theme.kindColor(F.labelKind(modelData.label))
                            border.color: "white"
                            border.width: 1.5
                            ToolTip.visible: markArea.containsMouse
                            ToolTip.text: modelData.label + " · " + F.dur(modelData.t)
                            MouseArea {
                                id: markArea
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                onClicked: { p.seek(Math.max(0, parent.modelData.t - 3)); player.play() }
                            }
                        }
                    }
                }

                // Controles
                RowLayout {
                    spacing: 10
                    AButton {
                        iconName: player.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                        onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                    }
                    Text { text: F.precise(p.position) + " / " + F.dur(p.duration); color: Theme.muted; font.pixelSize: 13 }
                    Item { Layout.fillWidth: true }
                    AButton {
                        id: muteButton
                        property bool muted: false
                        flat: true
                        iconName: muted ? "mute" : "volume"
                        onClicked: muted = !muted
                    }
                    Slider { id: volumeSlider; from: 0; to: 1; value: 0.8; Layout.preferredWidth: 120 }
                }

                // Corte
                Rectangle {
                    visible: p.trim !== null
                    Layout.fillWidth: true
                    implicitHeight: trimRow.implicitHeight + 20
                    radius: 10
                    color: Theme.panel
                    border.color: Theme.line
                    RowLayout {
                        id: trimRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10
                        Icon { name: "scissors"; size: 18 }
                        Text { text: "Início"; color: Theme.muted }
                        Text { text: p.trim ? F.precise(p.trim.a) : ""; color: Theme.text; font.bold: true }
                        AButton { text: "Marcar aqui"; onClicked: p.setMark("a") }
                        Text { text: "Fim"; color: Theme.muted }
                        Text { text: p.trim ? F.precise(p.trim.b) : ""; color: Theme.text; font.bold: true }
                        AButton { text: "Marcar aqui"; onClicked: p.setMark("b") }
                        Item { Layout.fillWidth: true }
                        Text { text: p.trim ? F.dur(p.trim.b - p.trim.a) + " · sem re-encode" : ""; color: Theme.muted; font.pixelSize: 12 }
                        AButton { text: "Cancelar"; onClicked: p.trim = null }
                        AButton {
                            text: p.trimming ? "Cortando…" : "Salvar corte"
                            primary: true
                            enabled: !p.trimming
                            onClicked: { p.trimming = true; App.trim(p.media.path, p.trim.a, p.trim.b) }
                        }
                    }
                }
            }

            // Desta partida
            Rectangle {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    Text {
                        text: p.match ? "Desta partida · " + F.time(p.match.start) : ""
                        color: Theme.muted
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 10
                        model: p.match ? p.match.clips : []
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: mini
                            required property var modelData
                            width: ListView.view.width
                            height: miniTile.implicitHeight + 34
                            radius: 10
                            color: p.media && modelData.id === p.media.id ? Theme.cardHi : "transparent"
                            border.color: p.media && modelData.id === p.media.id ? Theme.accent : "transparent"
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 4
                                ClipTile { id: miniTile; item: mini.modelData; compact: true; Layout.fillWidth: true }
                                Text {
                                    text: F.chipText(mini.modelData) + " · " + F.time(mini.modelData.created) + " · " + F.size(mini.modelData.size)
                                    color: Theme.muted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
