import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "Format.js" as F

// Player: vídeo (Qt Multimedia/FFmpeg: H.264 e HEVC sem codec da Store), linha do tempo com os
// eventos da partida (ícones por tipo), recorte sem re-encode, os vídeos da mesma partida em cima
// e a lista de eventos ao lado.
FocusScope {
    id: p
    property var media
    readonly property var match: media ? win.matchOf(media) : null
    readonly property var info: match && match.info ? match.info : ({})
    readonly property bool hasInfo: info.champion !== undefined
    readonly property var fullVideo: match ? match.clips.find(c => c.kind === "full") || null : null
    property var trim: null // { a, b } em segundos
    property bool trimming: false
    readonly property real duration: player.duration > 0 ? player.duration / 1000 : (media ? Math.max(0, win.durationOf(media)) : 0)
    readonly property real position: player.position / 1000
    readonly property bool fullscreen: win.playerFullscreen
    property bool idle: false // tela cheia sem mexer o mouse: esconde controles e cursor
    readonly property bool controlsVisible: !fullscreen || !idle || player.playbackState !== MediaPlayer.PlayingState
                                            || trim !== null || controlsHover.hovered
    focus: true
    onFullscreenChanged: wake()

    function wake() { idle = false; if (fullscreen) idleTimer.restart() }
    Timer { id: idleTimer; interval: 2500; onTriggered: p.idle = true }
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

    function setFullscreenToggle() { win.setPlayerFullscreen(!fullscreen); forceActiveFocus() }
    function seek(t) { player.position = Math.max(0, Math.min(duration, t)) * 1000 }
    function setMark(which) {
        if (!trim) return
        const t = Object.assign({}, trim)
        t[which] = position
        if (t.a > t.b) { const x = t.a; t.a = t.b; t.b = x }
        trim = t
    }

    Keys.onPressed: event => {
        wake()
        if (event.key === Qt.Key_Escape) { if (fullscreen) win.setPlayerFullscreen(false); else if (trim) trim = null; else win.current = null }
        else if (event.key === Qt.Key_F || event.key === Qt.Key_F11) win.setPlayerFullscreen(!fullscreen)
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
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) {
            if (win.pendingSeek >= 0) {
                p.seek(win.pendingSeek)
                win.pendingSeek = -1
            }
            play()
        }
    }

    YouTubeDialog {
        id: youtubeDialog
        onClosed: p.forceActiveFocus()
    }

    Connections {
        target: App
        function onTrimDone(ok, path) { p.trimming = false; if (ok) p.trim = null }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: p.fullscreen ? 0 : 20
        spacing: 14

        // Barra superior
        RowLayout {
            visible: !p.fullscreen
            spacing: 10
            AButton { text: "Voltar"; iconName: "back"; onClicked: win.current = null }
            Item {
                implicitWidth: 38
                implicitHeight: 38
                Rectangle {
                    anchors.fill: parent
                    visible: p.hasInfo
                    radius: 19
                    color: Theme.card
                    border.color: p.info.result === "Win" ? Theme.assist : p.info.result === "Lose" ? Theme.kill : Theme.line
                    border.width: 2
                    clip: true
                    Image {
                        anchors.fill: parent
                        anchors.margins: 2
                        source: p.info.championIcon || ""
                        sourceSize: Qt.size(76, 76)
                        fillMode: Image.PreserveAspectCrop
                    }
                }
                GameAvatar { anchors.centerIn: parent; visible: !p.hasInfo; game: p.media ? p.media.game : ""; iconUrl: p.media ? win.gameIcon(p.media.game) : ""; size: 36 }
            }
            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: !p.media ? ""
                        : p.media.kind === "full" && p.hasInfo ? p.info.champion + " · " + F.kda(p.info)
                        : F.chipText(p.media) + " · " + (p.hasInfo ? p.info.champion : F.gameName(p.media.game))
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: !p.media ? "" : [
                        F.modeText(p.info), F.resultText(p.info),
                        F.dayLabel(p.media.created) + ", " + F.time(p.media.created),
                        p.media.matchTime >= 0 ? F.dur(p.media.matchTime) + " na partida" : "",
                        F.size(p.media.size)
                    ].filter(x => x).join(" · ")
                    color: Theme.muted
                    font.pixelSize: 12
                }
            }
            AButton {
                visible: !!p.media && p.media.kind !== "full" && p.media.matchTime >= 0 && p.fullVideo !== null
                text: "Ver na partida"
                iconName: "clock"
                ToolTip.visible: hovered
                ToolTip.text: "Abre a partida completa neste momento (" + (p.media ? F.dur(p.media.matchTime) : "") + ")"
                onClicked: win.openAt(p.fullVideo, Math.max(0, p.media.matchTime))
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
            AButton {
                readonly property var yt: App.youtube
                readonly property bool sending: yt.uploading && youtubeDialog.norm(yt.uploadPath) === youtubeDialog.norm(p.media ? p.media.path : "")
                readonly property bool sent: { yt.uploads; return p.media ? yt.uploadedUrl(p.media.path) !== "" : false }
                text: sending ? "Enviando " + Math.floor(yt.progress * 100) + "%" : sent ? "No YouTube" : "YouTube"
                iconName: "youtube"
                iconColor: sent || sending ? "#ff0033" : Theme.text
                ToolTip.visible: hovered
                ToolTip.text: "Compartilhar este vídeo no YouTube"
                onClicked: youtubeDialog.openFor(p.media)
            }
            AButton {
                iconName: "folder"
                ToolTip.visible: hovered
                ToolTip.text: "Mostrar na pasta"
                onClicked: App.reveal(p.media.path)
            }
            AButton {
                iconName: "external"
                ToolTip.visible: hovered
                ToolTip.text: "Abrir no player do Windows"
                onClicked: App.openExternal(p.media.path)
            }
            AButton { text: "Excluir"; iconName: "trash"; danger: true; onClicked: win.confirmDelete(p.media) }
        }

        // Vídeos desta partida (partida completa + clipes), como no Outplayed.
        ListView {
            id: strip
            visible: !p.fullscreen && !!p.match && p.match.clips.length > 1
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            orientation: ListView.Horizontal
            spacing: 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: p.match ? p.match.clips : []
            FastWheel { horizontal: true }
            delegate: AbstractButton {
                id: mini
                required property var modelData
                readonly property bool on: !!p.media && modelData.id === p.media.id
                width: 224
                height: 66
                hoverEnabled: true
                onClicked: win.openClip(modelData)
                background: Rectangle {
                    radius: 10
                    color: mini.on ? Theme.cardHi : mini.hovered ? Theme.card : Theme.panel
                    border.color: mini.on ? Theme.accent : Theme.line
                }
                contentItem: RowLayout {
                    spacing: 8
                    Rectangle {
                        Layout.leftMargin: 6
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 54
                        radius: 6
                        color: Theme.card
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: win.thumbOf(mini.modelData)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 200
                        }
                        Rectangle {
                            anchors { right: parent.right; bottom: parent.bottom; margins: 4 }
                            radius: 4; height: 15; width: miniDur.implicitWidth + 8
                            color: "#cc000000"
                            Text { id: miniDur; anchors.centerIn: parent; text: F.dur(win.durationOf(mini.modelData)); color: "white"; font.pixelSize: 10 }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: 8
                        spacing: 3
                        RowLayout {
                            spacing: 5
                            EventBadge {
                                label: mini.modelData.label
                                icon: F.clipIcon(mini.modelData)
                                color: Theme.kindColor(F.kindOf(mini.modelData))
                                size: 18
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: F.chipText(mini.modelData)
                                color: Theme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            text: mini.modelData.matchTime >= 0 ? F.dur(mini.modelData.matchTime) + " na partida" : F.time(mini.modelData.created)
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // Vídeo + controles. Em tela cheia o vídeo ocupa tudo e os controles ficam por cima,
            // sumindo depois de um tempo sem mexer o mouse (enquanto toca).
            Item {
                id: stage
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    anchors.bottom: p.fullscreen ? parent.bottom : controls.top
                    anchors.bottomMargin: p.fullscreen ? 0 : 10
                    radius: p.fullscreen ? 0 : Theme.radius
                    color: "black"
                    clip: true
                    VideoOutput {
                        id: video
                        anchors.fill: parent
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: p.fullscreen
                        cursorShape: p.controlsVisible ? Qt.ArrowCursor : Qt.BlankCursor
                        onPositionChanged: p.wake()
                        onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                        onDoubleClicked: win.setPlayerFullscreen(!p.fullscreen)
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

                // Sombra atrás dos controles em tela cheia, para lerem sobre o vídeo.
                Rectangle {
                    visible: p.fullscreen
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: controls.height + 60
                    opacity: controls.opacity
                    gradient: Gradient {
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.75) }
                    }
                }

                ColumnLayout {
                    id: controls
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    spacing: 10
                    opacity: p.controlsVisible ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity { NumberAnimation { duration: 250 } }
                    HoverHandler { id: controlsHover }

                    // Linha do tempo com marcadores de eventos e a faixa do corte
                    Item {
                        id: timeline
                        Layout.fillWidth: true
                        Layout.leftMargin: p.fullscreen ? 16 : 0
                        Layout.rightMargin: p.fullscreen ? 16 : 0
                        Layout.preferredHeight: 30
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
                            delegate: EventBadge {
                                required property var modelData
                                label: modelData.label
                                size: markArea.containsMouse ? 24 : 20
                                x: modelData.t / p.duration * timeline.width - size / 2
                                anchors.verticalCenter: parent.verticalCenter
                                border.color: Theme.bg
                                border.width: 1.5
                                z: markArea.containsMouse ? 2 : 1
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
                        Layout.leftMargin: p.fullscreen ? 16 : 0
                        Layout.rightMargin: p.fullscreen ? 16 : 0
                        Layout.bottomMargin: p.fullscreen ? 12 : 0
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
                        AButton {
                            flat: true
                            iconName: p.fullscreen ? "shrink" : "expand"
                            ToolTip.visible: hovered
                            ToolTip.text: p.fullscreen ? "Sair da tela cheia (Esc)" : "Tela cheia (F)"
                            onClicked: p.setFullscreenToggle()
                        }
                    }

                    // Corte
                    Rectangle {
                        visible: p.trim !== null
                        Layout.fillWidth: true
                        Layout.leftMargin: p.fullscreen ? 16 : 0
                        Layout.rightMargin: p.fullscreen ? 16 : 0
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
            }

            // Desta partida
            Rectangle {
                visible: !p.fullscreen
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
                        text: "Eventos" + (p.media && p.media.markers.length ? " · " + p.media.markers.length : "")
                        color: Theme.muted
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Text {
                        visible: !p.media || p.media.markers.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "Nenhum evento marcado neste vídeo."
                        color: Theme.faint
                        font.pixelSize: 12
                    }
                    ListView {
                        id: eventList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: p.media ? p.media.markers : []
                        ScrollBar.vertical: ScrollBar {}
                        FastWheel {}
                        delegate: AbstractButton {
                            id: evRow
                            required property var modelData
                            readonly property bool now: p.position >= modelData.t - 3 && p.position < modelData.t + 4
                            width: ListView.view.width
                            height: 38
                            hoverEnabled: true
                            onClicked: { p.seek(Math.max(0, modelData.t - 3)); player.play() }
                            background: Rectangle {
                                radius: 8
                                color: evRow.now ? Theme.cardHi : evRow.hovered ? Theme.card : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 10
                                EventBadge { Layout.leftMargin: 6; label: evRow.modelData.label; size: 24 }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: evRow.modelData.label
                                    color: Theme.text
                                    font.pixelSize: 13
                                }
                                Text {
                                    Layout.rightMargin: 8
                                    text: F.dur(evRow.modelData.t + (p.media.kind !== "full" && p.media.matchTime >= 0 ? p.media.matchTime : 0))
                                    color: Theme.muted
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
