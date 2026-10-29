import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "Format.js" as F

// Player: vídeo (Qt Multimedia/FFmpeg: H.264 e HEVC sem codec da Store), linha do tempo com os
// eventos da partida (ícones por tipo), recorte sem re-encode e, ao lado, o filtro e a lista de eventos.
// `embedded`: dentro da tela de Sessões (sem "Voltar"; título da sessão vem de fora).
FocusScope {
    id: p
    property var media
    property bool embedded: false
    property string title: ""    // substitui o título gerado (ex.: "Sessão 01")
    property string subtitle: ""
    readonly property var match: media ? win.matchOf(media) : null
    readonly property var info: match && match.info ? match.info : ({})
    readonly property bool hasInfo: info.champion !== undefined
    readonly property var fullVideo: match ? match.clips.find(c => c.kind === "full") || null : null
    // Filtro de eventos por tipo (ícone), como o "Filter events" do Outplayed.
    property var hiddenKinds: ({})
    function toggleKind(icon) {
        const h = Object.assign({}, hiddenKinds)
        if (h[icon]) delete h[icon]
        else h[icon] = true
        hiddenKinds = h
    }
    readonly property var allMarkers: media ? media.markers : []
    readonly property var kinds: F.eventSummary(allMarkers)
    readonly property var visibleMarkers: allMarkers.filter(m => !hiddenKinds[F.eventIcon(m.label)])
    // Eventos visíveis em ordem (anterior/próximo).
    readonly property var markerTimes: visibleMarkers.map(m => m.t).sort((a, b) => a - b)
    function prevEvent() {
        const before = markerTimes.filter(t => t - 3 < position - 1.5)
        seek(before.length ? Math.max(0, before[before.length - 1] - 3) : 0)
    }
    function nextEvent() {
        const after = markerTimes.find(t => t - 3 > position + 0.5)
        if (after !== undefined) seek(after - 3)
    }
    function setRate(r) { player.playbackRate = r }
    // Clipes sugeridos: eventos escolhidos na lista viram clipes (os próximos se juntam num só).
    property bool picking: false
    property var picked: ({})
    property bool creatingClips: false
    function pickKey(m) { return m.t + "|" + m.label }
    function togglePick(m) {
        const s = Object.assign({}, picked)
        if (s[pickKey(m)]) delete s[pickKey(m)]
        else s[pickKey(m)] = m
        picked = s
    }
    readonly property int pickedCount: Object.keys(picked).length
    function createClips() {
        creatingClips = true
        App.clipsFromEvents(media.path, Object.keys(picked).map(k => ({ t: picked[k].t, label: picked[k].label })))
    }
    property bool priming: false // carregando o primeiro quadro (mudo), depois pausa
    property bool started: false // já tocou ou mudou de posição: a capa some
    property string primedUrl: "" // vídeo cujo primeiro quadro já foi carregado (só uma vez por vídeo)
    property string poster: ""    // capa em alta resolução (gerada ao abrir; até lá, a miniatura)
    function togglePlay() {
        priming = false
        started = true
        player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
    }
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

    // Zoom da linha do tempo: trecho visível [viewA, viewA + viewLen]. `zoomLen` 0 = vídeo inteiro.
    property real zoomLen: 0
    property real zoomStart: 0
    readonly property real viewLen: zoomLen > 0 && zoomLen < duration ? zoomLen : duration
    readonly property real viewA: Math.max(0, Math.min(duration - viewLen, zoomStart))
    readonly property bool zoomed: viewLen < duration
    function setView(a, len) {
        len = Math.max(Math.min(2, duration), Math.min(duration, len))
        zoomLen = len >= duration ? 0 : len
        zoomStart = Math.max(0, Math.min(duration - len, a))
    }
    // Zoom mantendo o instante `t` no mesmo lugar da linha do tempo.
    function zoomAt(t, factor) {
        const frac = viewLen > 0 ? (t - viewA) / viewLen : 0
        const len = viewLen * factor
        setView(t - frac * Math.max(Math.min(2, duration), Math.min(duration, len)), len)
    }
    function zoomToTrim() {
        if (!trim) return
        const pad = Math.max(1, (trim.b - trim.a) * 0.15)
        setView(trim.a - pad, trim.b - trim.a + 2 * pad)
    }
    // Tocando com zoom: a janela acompanha a posição.
    onPositionChanged: if (zoomed && !timelineArea.pressed && (position < viewA || position > viewA + viewLen))
        zoomStart = position - viewLen * 0.1

    function wake() { idle = false; if (fullscreen) idleTimer.restart() }
    Timer { id: idleTimer; interval: 2500; onTriggered: p.idle = true }
    Component.onCompleted: {
        forceActiveFocus()
        if (media)
            player.source = media.url
    }

    onMediaChanged: {
        trim = null
        picking = false
        picked = ({})
        zoomLen = 0
        zoomStart = 0
        started = win.pendingSeek >= 0
        poster = ""
        if (media)
            App.requestPoster(media.id, media.path)
        if (media && player.source.toString() !== media.url)
            player.source = media.url
    }

    function setFullscreenToggle() { win.setPlayerFullscreen(!fullscreen); forceActiveFocus() }
    function seek(t) { started = true; priming = false; player.position = Math.max(0, Math.min(duration, t)) * 1000 }
    // Ajuste de uma ponta (alça arrastada, ±0,1 s): não cruza a outra e mantém pelo menos meio segundo.
    function setMarkAt(which, time) {
        if (!trim) return
        const t = Object.assign({}, trim)
        time = Math.max(0, Math.min(duration, time))
        t[which] = which === "a" ? Math.min(time, t.b - 0.5) : Math.max(time, t.a + 0.5)
        trim = t
    }
    // Começa o corte: vídeo curto, inteiro; longo, 30 s a partir da posição atual (com zoom nesse trecho).
    function startTrim() {
        if (duration <= 60) { trim = { a: 0, b: duration }; return }
        const b = Math.min(duration, position + 30)
        trim = { a: Math.max(0, b - 30), b: b }
        zoomToTrim()
    }

    Keys.onPressed: event => {
        wake()
        if (event.key === Qt.Key_Escape) { if (fullscreen) win.setPlayerFullscreen(false); else if (trim) trim = null; else if (!embedded) win.current = null }
        else if (event.key === Qt.Key_F || event.key === Qt.Key_F11) win.setPlayerFullscreen(!fullscreen)
        else if (event.key === Qt.Key_Space) p.togglePlay()
        else if (event.key === Qt.Key_Right) seek(position + 5)
        else if (event.key === Qt.Key_Left) seek(position - 5)
        else if (event.key === Qt.Key_Period) { player.pause(); seek(position + 1 / 60) }
        else if (event.key === Qt.Key_Comma) { player.pause(); seek(position - 1 / 60) }
        else if (event.key === Qt.Key_I && trim) setMarkAt("a", position)
        else if (event.key === Qt.Key_O && trim) setMarkAt("b", position)
        else if (event.key === Qt.Key_Delete) win.confirmDelete(media)
        else return
        event.accepted = true
    }

    MediaPlayer {
        id: player
        videoOutput: video
        onPlaybackStateChanged: if (playbackState === MediaPlayer.PlayingState && !p.priming) p.started = true
        audioOutput: AudioOutput { id: audio; volume: volumeSlider.value; muted: muteButton.muted || p.priming }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia && p.media && p.primedUrl !== p.media.url) {
            p.primedUrl = p.media.url
            if (win.pendingSeek >= 0) {
                p.seek(win.pendingSeek)
                win.pendingSeek = -1
            }
            // Sem play automático: toca em silêncio só até o primeiro quadro aparecer e pausa ali.
            p.priming = true
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
        function onEventClipsDone(saved) { p.creatingClips = false; if (saved > 0) { p.picking = false; p.picked = ({}) } }
    }
    Connections {
        target: App
        function onPosterReady(id, url) { if (p.media && p.media.id === id) p.poster = url }
    }
    Connections {
        target: video.videoSink
        function onVideoFrameChanged() {
            if (p.priming) {
                player.pause()
                p.priming = false
            }
        }
    }
    // Excluindo este vídeo: solta o arquivo antes.
    Connections {
        target: win
        function onDeletingPathChanged() {
            if (p.media && win.deletingPath === p.media.path) {
                player.stop()
                player.source = ""
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: p.fullscreen || p.embedded ? 0 : 20
        spacing: 14

        // Barra superior
        RowLayout {
            visible: !p.fullscreen
            spacing: 10
            AButton { visible: !p.embedded; text: "Voltar"; iconName: "back"; onClicked: win.current = null }
            MatchAvatar { match: p.match; size: 38 }
            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: p.title !== "" ? p.title
                        : !p.media ? ""
                        : p.media.kind === "full" && p.hasInfo ? p.info.champion + " · " + F.kda(p.info)
                        : F.chipText(p.media) + " · " + (p.hasInfo ? p.info.champion : F.gameName(p.media.game))
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: p.subtitle !== "" ? p.subtitle : !p.media ? "" : [
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
                visible: !p.embedded && !!p.media && p.media.kind !== "full" && p.media.matchTime >= 0 && p.fullVideo !== null
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
            AButton { text: "Cortar"; iconName: "scissors"; enabled: p.duration > 0; onClicked: p.startTrim() }
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
                    // Capa: um quadro do meio do vídeo (a miniatura), até o primeiro play.
                    Image {
                        anchors.fill: parent
                        visible: !p.started && p.media !== null && status === Image.Ready
                        source: p.poster !== "" ? p.poster : p.media ? win.thumbOf(p.media) : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: p.fullscreen
                        cursorShape: p.controlsVisible ? Qt.ArrowCursor : Qt.BlankCursor
                        onPositionChanged: p.wake()
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

                    // Linha do tempo com marcadores de eventos e a faixa do corte. Roda do mouse: zoom no
                    // ponto do cursor; Shift + roda (ou roda horizontal): anda pelo trecho ampliado.
                    Item {
                        id: timeline
                        Layout.fillWidth: true
                        Layout.leftMargin: p.fullscreen ? 16 : 0
                        Layout.rightMargin: p.fullscreen ? 16 : 0
                        Layout.preferredHeight: p.zoomed ? 54 : 46
                        clip: true
                        function tx(t) { return p.viewLen > 0 ? (t - p.viewA) / p.viewLen * width : 0 }
                        function tAt(x) { return p.viewA + Math.max(0, Math.min(width, x)) / width * p.viewLen }
                        // Marcas de tempo, como no Outplayed: o passo cresce com o trecho visível (~10 marcas).
                        readonly property real tick: {
                            const steps = [0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800]
                            return steps.find(s => p.viewLen / s <= 12) || 3600
                        }
                        readonly property int firstTick: Math.ceil(p.viewA / tick - 1e-6)
                        Repeater {
                            model: p.viewLen > 0 ? Math.floor((p.viewA + p.viewLen) / timeline.tick + 1e-6) - timeline.firstTick + 1 : 0
                            Item {
                                required property int index
                                readonly property real t: (timeline.firstTick + index) * timeline.tick
                                x: timeline.tx(t)
                                y: 32
                                Rectangle { x: 0; y: -5; width: 1; height: 4; color: Theme.faint }
                                Text {
                                    x: parent.x < 1 ? 0 : -width / 2
                                    visible: x + parent.x + width <= timeline.width
                                    text: timeline.tick >= 60 ? Math.round(parent.t / 60) + "m"
                                        : timeline.tick < 1 ? F.precise(parent.t) : F.dur(parent.t)
                                    color: Theme.faint
                                    font.pixelSize: 10
                                }
                            }
                        }
                        Rectangle {
                            id: track
                            y: 10
                            width: parent.width
                            height: 6
                            radius: 3
                            color: Theme.cardHi
                        }
                        Rectangle {
                            visible: p.trim !== null && p.duration > 0
                            anchors.verticalCenter: track.verticalCenter
                            x: p.trim ? timeline.tx(p.trim.a) : 0
                            width: p.trim ? timeline.tx(p.trim.b) - x : 0
                            height: 14
                            radius: 4
                            color: Qt.rgba(0.545, 0.36, 0.965, 0.35)
                            border.color: Theme.accent
                        }
                        Rectangle {
                            anchors.verticalCenter: track.verticalCenter
                            width: p.duration > 0 ? Math.max(0, timeline.tx(p.position)) : 0
                            height: 6
                            radius: 3
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.gradA }
                                GradientStop { position: 1; color: Theme.gradB }
                            }
                        }
                        MouseArea {
                            id: timelineArea
                            anchors.fill: parent
                            onPressed: mouse => p.seek(timeline.tAt(mouse.x))
                            onPositionChanged: mouse => { if (pressed) p.seek(timeline.tAt(mouse.x)) }
                            onWheel: wheel => {
                                if (p.duration <= 0) return
                                const dx = wheel.angleDelta.x !== 0 ? wheel.angleDelta.x : (wheel.modifiers & Qt.ShiftModifier) ? wheel.angleDelta.y : 0
                                if (dx !== 0)
                                    p.setView(p.viewA - dx / 120 * p.viewLen * 0.15, p.viewLen)
                                else if (wheel.angleDelta.y !== 0)
                                    p.zoomAt(timeline.tAt(wheel.x), Math.pow(0.8, wheel.angleDelta.y / 120))
                            }
                        }
                        Repeater {
                            model: p.duration > 0 ? p.visibleMarkers : []
                            delegate: EventBadge {
                                required property var modelData
                                label: modelData.label
                                size: markArea.containsMouse ? 24 : 20
                                visible: modelData.t >= p.viewA - 1 && modelData.t <= p.viewA + p.viewLen + 1
                                x: timeline.tx(modelData.t) - size / 2
                                anchors.verticalCenter: track.verticalCenter
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
                        // Corte: arrastar a faixa move o trecho todo; as alças nas pontas ajustam início e fim.
                        MouseArea {
                            visible: p.trim !== null && p.duration > 0
                            x: p.trim ? timeline.tx(p.trim.a) + 6 : 0
                            width: p.trim ? Math.max(0, timeline.tx(p.trim.b) - timeline.tx(p.trim.a) - 12) : 0
                            anchors.verticalCenter: track.verticalCenter
                            height: 14
                            z: 3
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            property real grabT: 0
                            property var grabTrim: null
                            onPressed: mouse => { grabT = timeline.tAt(mapToItem(timeline, mouse.x, 0).x); grabTrim = p.trim }
                            onPositionChanged: mouse => {
                                if (!pressed || !grabTrim) return
                                const len = grabTrim.b - grabTrim.a
                                const a = Math.max(0, Math.min(p.duration - len, grabTrim.a + timeline.tAt(mapToItem(timeline, mouse.x, 0).x) - grabT))
                                p.trim = { a: a, b: a + len }
                                p.seek(a)
                            }
                        }
                        Repeater {
                            model: p.trim !== null && p.duration > 0 ? ["a", "b"] : []
                            Rectangle {
                                id: handle
                                required property string modelData
                                readonly property real t: p.trim ? p.trim[modelData] : 0
                                x: timeline.tx(t) - width / 2
                                anchors.verticalCenter: track.verticalCenter
                                width: 10
                                height: 24
                                radius: 3
                                z: 4
                                color: handleArea.pressed || handleArea.containsMouse ? Qt.lighter(Theme.accent, 1.2) : Theme.accent
                                border.color: Theme.bg
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    Repeater { model: 3; Rectangle { width: 4; height: 1; color: "white"; opacity: 0.8 } }
                                }
                                ToolTip.visible: handleArea.containsMouse || handleArea.pressed
                                ToolTip.text: (modelData === "a" ? "Início " : "Fim ") + F.precise(t)
                                MouseArea {
                                    id: handleArea
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    hoverEnabled: true
                                    cursorShape: Qt.SizeHorCursor
                                    onPositionChanged: mouse => {
                                        if (!pressed) return
                                        p.setMarkAt(handle.modelData, timeline.tAt(mapToItem(timeline, mouse.x, 0).x))
                                        p.seek(handle.t)
                                    }
                                    onPressed: p.seek(handle.t)
                                }
                            }
                        }
                        // Com zoom: o vídeo inteiro em miniatura, com o trecho visível arrastável.
                        Rectangle {
                            visible: p.zoomed
                            y: parent.height - 5
                            width: parent.width
                            height: 4
                            radius: 2
                            color: Theme.cardHi
                            Rectangle {
                                visible: p.trim !== null
                                x: p.trim ? p.trim.a / p.duration * parent.width : 0
                                width: p.trim ? Math.max(2, (p.trim.b - p.trim.a) / p.duration * parent.width) : 0
                                height: parent.height
                                color: Qt.rgba(0.545, 0.36, 0.965, 0.6)
                            }
                            Rectangle {
                                x: p.duration > 0 ? p.viewA / p.duration * parent.width : 0
                                width: p.duration > 0 ? Math.max(6, p.viewLen / p.duration * parent.width) : 0
                                height: parent.height
                                radius: 2
                                color: overviewArea.containsMouse || overviewArea.pressed ? Theme.text : Theme.muted
                            }
                            MouseArea {
                                id: overviewArea
                                anchors.fill: parent
                                anchors.topMargin: -6
                                hoverEnabled: true
                                cursorShape: Qt.SizeHorCursor
                                function go(x) { p.setView(x / width * p.duration - p.viewLen / 2, p.viewLen) }
                                onPressed: mouse => go(mouse.x)
                                onPositionChanged: mouse => { if (pressed) go(mouse.x) }
                            }
                        }
                    }

                    // Controles
                    RowLayout {
                        Layout.leftMargin: p.fullscreen ? 16 : 0
                        Layout.rightMargin: p.fullscreen ? 16 : 0
                        Layout.bottomMargin: p.fullscreen ? 12 : 0
                        spacing: 10
                        Text { text: F.precise(p.position) + " / " + F.dur(p.duration); color: Theme.muted; font.pixelSize: 13; Layout.preferredWidth: 120 }
                        Item { Layout.fillWidth: true }
                        AButton {
                            flat: true
                            iconName: "skipBack"
                            enabled: p.markerTimes.length > 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Evento anterior"
                            onClicked: p.prevEvent()
                        }
                        // Clique: volta 10 s. Segurando: volta acelerado.
                        AButton {
                            id: rewindButton
                            property int repeats: 0
                            flat: true
                            iconName: "rewind"
                            autoRepeat: true
                            autoRepeatDelay: 350
                            autoRepeatInterval: 70
                            onPressedChanged: if (pressed) repeats = 0
                            onClicked: { p.seek(p.position - (repeats++ === 0 && !pressed ? 10 : 1)) }
                            ToolTip.visible: hovered && !pressed
                            ToolTip.text: "Voltar 10 s (segure para voltar acelerado)"
                        }
                        AButton {
                            iconName: player.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                            onClicked: p.togglePlay()
                        }
                        // Clique: avança 10 s. Segurando: avança acelerado.
                        AButton {
                            property int repeats: 0
                            flat: true
                            iconName: "fastForward"
                            autoRepeat: true
                            autoRepeatDelay: 350
                            autoRepeatInterval: 70
                            onPressedChanged: if (pressed) repeats = 0
                            onClicked: { p.seek(p.position + (repeats++ === 0 && !pressed ? 10 : 1)) }
                            ToolTip.visible: hovered && !pressed
                            ToolTip.text: "Avançar 10 s (segure para avançar acelerado)"
                        }
                        AButton {
                            flat: true
                            iconName: "skipForward"
                            enabled: p.markerTimes.length > 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Próximo evento"
                            onClicked: p.nextEvent()
                        }
                        AButton {
                            id: speedButton
                            flat: true
                            text: player.playbackRate + "x"
                            ToolTip.visible: hovered
                            ToolTip.text: "Velocidade"
                            onClicked: speedMenu.open()
                            Menu {
                                id: speedMenu
                                y: -implicitHeight - 4
                                Repeater {
                                    model: [0.25, 0.5, 1, 1.5, 2, 4]
                                    MenuItem {
                                        required property var modelData
                                        text: modelData + "x"
                                        checkable: true
                                        checked: player.playbackRate === modelData
                                        onTriggered: p.setRate(modelData)
                                    }
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        AButton {
                            flat: true
                            iconName: "zoomOut"
                            enabled: p.zoomed
                            ToolTip.visible: hovered
                            ToolTip.text: "Diminuir o zoom da linha do tempo (roda do mouse sobre ela)"
                            onClicked: p.zoomAt(p.position, 2)
                        }
                        AButton {
                            flat: true
                            iconName: "zoomIn"
                            enabled: p.duration > 0 && p.viewLen > Math.min(2, p.duration) + 0.01
                            ToolTip.visible: hovered
                            ToolTip.text: "Aumentar o zoom na posição atual (roda do mouse sobre a linha do tempo)"
                            onClicked: p.zoomAt(p.position, 0.5)
                        }
                        AButton {
                            visible: p.zoomed
                            flat: true
                            text: "Tudo"
                            ToolTip.visible: hovered
                            ToolTip.text: "Mostrar o vídeo inteiro na linha do tempo"
                            onClicked: p.setView(0, p.duration)
                        }
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
                            // Início e fim: clique no tempo vai até ele; ‹ › ajustam 0,1 s; "Aqui" (I / O) usa a posição atual.
                            Repeater {
                                model: [{ k: "a", name: "Início", key: "I" }, { k: "b", name: "Fim", key: "O" }]
                                RowLayout {
                                    required property var modelData
                                    spacing: 4
                                    Text { text: modelData.name; color: Theme.muted; Layout.rightMargin: 4 }
                                    AButton { flat: true; text: "‹"; implicitWidth: 26; onClicked: { p.setMarkAt(modelData.k, p.trim[modelData.k] - 0.1); p.seek(p.trim[modelData.k]) } }
                                    Text {
                                        text: p.trim ? F.precise(p.trim[modelData.k]) : ""
                                        color: Theme.text
                                        font.bold: true
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: p.seek(p.trim[parent.parent.modelData.k])
                                        }
                                    }
                                    AButton { flat: true; text: "›"; implicitWidth: 26; onClicked: { p.setMarkAt(modelData.k, p.trim[modelData.k] + 0.1); p.seek(p.trim[modelData.k]) } }
                                    AButton {
                                        text: "Aqui"
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Marcar na posição atual (" + modelData.key + ")"
                                        onClicked: p.setMarkAt(modelData.k, p.position)
                                    }
                                    Item { implicitWidth: 6 }
                                }
                            }
                            AButton {
                                flat: true
                                iconName: "zoomIn"
                                ToolTip.visible: hovered
                                ToolTip.text: "Ampliar a linha do tempo no trecho do corte"
                                onClicked: p.zoomToTrim()
                            }
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

            // Eventos: filtro por tipo (liga/desliga na linha do tempo) e a lista.
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
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Filtrar eventos"
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Text {
                            visible: !p.picking && p.allMarkers.length > 0
                            text: "Criar clipes"
                            color: Theme.accent
                            font.pixelSize: 12
                            ToolTip.visible: createClipsArea.containsMouse
                            ToolTip.text: "Escolha os eventos que viram clipe"
                            MouseArea { id: createClipsArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: p.picking = true }
                        }
                        Text {
                            visible: Object.keys(p.hiddenKinds).length > 0
                            Layout.leftMargin: 8
                            text: "Mostrar todos"
                            color: Theme.accent
                            font.pixelSize: 12
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: p.hiddenKinds = ({}) }
                        }
                    }
                    Text {
                        visible: p.kinds.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "Nenhum evento marcado neste vídeo."
                        color: Theme.faint
                        font.pixelSize: 12
                    }
                    Repeater {
                        model: p.kinds
                        delegate: AbstractButton {
                            id: kindRow
                            required property var modelData
                            readonly property bool shown: !p.hiddenKinds[modelData.icon]
                            Layout.fillWidth: true
                            implicitHeight: 34
                            hoverEnabled: true
                            onClicked: p.toggleKind(modelData.icon)
                            background: Rectangle { radius: 8; color: kindRow.hovered ? Theme.card : "transparent" }
                            contentItem: RowLayout {
                                spacing: 10
                                EventBadge { Layout.leftMargin: 4; label: kindRow.modelData.label; size: 22; opacity: kindRow.shown ? 1 : 0.35 }
                                Text {
                                    Layout.fillWidth: true
                                    text: F.kindName(kindRow.modelData.icon)
                                    color: kindRow.shown ? Theme.text : Theme.faint
                                    font.pixelSize: 13
                                }
                                Text { text: kindRow.modelData.count; color: Theme.muted; font.pixelSize: 12 }
                                // Liga/desliga
                                Rectangle {
                                    Layout.rightMargin: 4
                                    width: 30; height: 16; radius: 8
                                    color: kindRow.shown ? Theme.accent : Theme.cardHi
                                    Rectangle {
                                        width: 12; height: 12; radius: 6
                                        y: 2
                                        x: kindRow.shown ? parent.width - width - 2 : 2
                                        color: "white"
                                        Behavior on x { NumberAnimation { duration: 120 } }
                                    }
                                }
                            }
                        }
                    }
                    Rectangle { visible: p.visibleMarkers.length > 0; Layout.fillWidth: true; Layout.topMargin: 4; height: 1; color: Theme.line }
                    ListView {
                        id: eventList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: p.visibleMarkers
                        ScrollBar.vertical: ScrollBar {}
                        FastWheel {}
                        delegate: AbstractButton {
                            id: evRow
                            required property var modelData
                            readonly property bool now: p.position >= modelData.t - 3 && p.position < modelData.t + 4
                            width: ListView.view.width
                            height: 38
                            hoverEnabled: true
                            readonly property bool chosen: !!p.picked[p.pickKey(modelData)]
                            onClicked: {
                                p.seek(Math.max(0, modelData.t - 3))
                                if (p.picking) p.togglePick(modelData)
                                else player.play()
                            }
                            background: Rectangle {
                                radius: 8
                                color: evRow.now ? Theme.cardHi : evRow.hovered ? Theme.card : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 10
                                Rectangle {
                                    visible: p.picking
                                    Layout.leftMargin: 6
                                    implicitWidth: 18; implicitHeight: 18; radius: 5
                                    color: evRow.chosen ? Theme.accent : "transparent"
                                    border.color: evRow.chosen ? Theme.accent : Theme.muted
                                    Icon { anchors.centerIn: parent; visible: evRow.chosen; name: "check"; size: 14; color: "white" }
                                }
                                EventBadge { Layout.leftMargin: p.picking ? 0 : 6; label: evRow.modelData.label; size: 24 }
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
                    // Clipes sugeridos: escolha dos eventos
                    ColumnLayout {
                        visible: p.picking
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: p.pickedCount === 0 ? "Marque os eventos que viram clipe."
                                    : F.plural(p.pickedCount, "evento", "eventos") + " · os próximos viram um clipe só"
                                color: Theme.muted
                                font.pixelSize: 12
                            }
                            Text {
                                text: p.pickedCount > 0 && p.pickedCount >= p.visibleMarkers.length ? "Nenhum" : "Todos"
                                color: Theme.accent
                                font.pixelSize: 12
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (p.pickedCount > 0 && p.pickedCount >= p.visibleMarkers.length) { p.picked = ({}); return }
                                        const s = {}
                                        for (const m of p.visibleMarkers) s[p.pickKey(m)] = m
                                        p.picked = s
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            AButton { text: "Cancelar"; enabled: !p.creatingClips; onClicked: { p.picking = false; p.picked = ({}) } }
                            Item { Layout.fillWidth: true }
                            AButton {
                                text: p.creatingClips ? "Criando…" : "Criar clipes"
                                iconName: "scissors"
                                primary: true
                                enabled: p.pickedCount > 0 && !p.creatingClips
                                onClicked: p.createClips()
                            }
                        }
                    }
                }
            }
        }
    }
}
