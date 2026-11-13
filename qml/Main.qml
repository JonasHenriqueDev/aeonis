import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Janela principal: coluna de ícones + Sessões (tela inicial) / Clipes / Configurações / Sobre, com o
// player de clipes por cima. Criada ao abrir e destruída ao fechar (WindowHost).
ApplicationWindow {
    id: win
    width: 1380
    height: 880
    minimumWidth: 980
    minimumHeight: 620
    visible: true
    title: "Aeonis"
    color: Theme.bg
    flags: Qt.Window | Qt.FramelessWindowHint // barra de título própria (TitleBar.qml + WindowHost)
    font.family: Theme.font

    property string view: ["settings", "about", "clips"].includes(InitialPage) ? InitialPage : "sessions"
    property var current: null        // clipe aberto no player (por cima das telas)
    // Seleção da tela de Sessões (vazio = a mais recente).
    property string selGame: ""
    property string selDay: ""        // aaaa-mm-dd
    property string selSession: ""
    property string selVideo: ""      // vídeo da sessão (padrão: a partida completa)
    property string deletingPath: ""  // vídeo sendo excluído: o player solta o arquivo
    property string pendingMatch: ""  // partida a abrir assim que a biblioteca carregar ("match:<id>")
    property real pendingSeek: -1     // posição inicial (s) do próximo vídeo aberto no player
    property var thumbs: ({})         // id -> { url, duration } gerados depois da varredura
    property int thumbTick: 0
    property bool openedPlayer: false
    property bool debugPlayer: InitialPage === "player"
    property bool playerFullscreen: false // vídeo ocupando a tela toda
    // Lidas pelo TitleBarFilter (C++) para saber onde arrastar a janela.
    readonly property real titleBarHeight: titleBar.visible ? titleBar.height : 0
    readonly property real titleBarButtonsWidth: titleBar.buttonsWidth
    property int visibilityBeforeFullscreen: Window.Windowed

    function setPlayerFullscreen(on) {
        if (on === playerFullscreen) return
        if (on) {
            visibilityBeforeFullscreen = visibility
            playerFullscreen = true
            showFullScreen()
        } else {
            playerFullscreen = false
            if (visibilityBeforeFullscreen === Window.Maximized) showMaximized()
            else showNormal()
        }
    }
    onCurrentChanged: if (!current) setPlayerFullscreen(false)

    readonly property var lib: App.library

    function go(v) {
        if (v === "player") { debugPlayer = true; openDebugPlayer(); return }
        if (v === "deadlock") { openDeadlockTip(); return }
        if (v.startsWith("match:")) {
            pendingMatch = v.slice(6)
            openPendingMatch()
            return
        }
        if (v === "library" || v === "matches") v = "sessions"
        current = null
        view = v
    }
    function openClip(c) { current = c }
    // Abre a sessão (partida) na tela de Sessões.
    function openMatch(m) {
        current = null
        view = "sessions"
        selGame = m.game
        selDay = F.dayKey(m.start)
        selSession = m.id
        selVideo = ""
    }
    // Abre o vídeo `c` (ex.: a partida completa) da sua sessão já na posição `t` segundos.
    function openAt(c, t) {
        const m = matchOf(c)
        if (!m) return
        pendingSeek = t
        openMatch(m)
        selVideo = c.id
    }
    function openPendingMatch() {
        if (!pendingMatch) return false
        const m = (App.library.matches || []).find(x => x.id === pendingMatch)
        if (!m) return false
        pendingMatch = ""
        openMatch(m)
        return true
    }
    function thumbOf(c) { thumbTick; const t = thumbs[c.id]; return t && t.url ? t.url : c.thumb }
    function durationOf(c) { thumbTick; const t = thumbs[c.id]; return c.duration >= 0 ? c.duration : (t ? t.duration : -1) }
    // Ícone embutido do catálogo ou o extraído do jogo (reavaliado quando a biblioteca muda).
    function gameIcon(name) { lib; return name ? App.gameIconUrl(name) : "" }
    function matchOf(c) {
        for (const m of (App.library.matches || []))
            for (const x of m.clips)
                if (x.id === c.id) return m
        return null
    }
    function findClip(id) {
        for (const m of (App.library.matches || []))
            for (const x of m.clips)
                if (x.id === id) return x
        return null
    }

    Connections {
        target: App
        function onNavigate(page) { win.go(page) }
        function onToast(text, kind) { toasts.show(text, kind) }
        function onThumbReady(id, url, duration) {
            win.thumbs[id] = { url: url, duration: duration }
            win.thumbTick++
        }
        function onLibraryChanged() {
            win.deletingPath = ""
            if (win.openDebugPlayer() || win.openPendingMatch())
                return
            if (win.current) {
                const c = win.findClip(win.current.id)
                win.current = c
            }
        }
    }

    TitleBar {
        id: titleBar
        visible: !win.playerFullscreen
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: visible ? 36 : 0
    }

    RowLayout {
        anchors { left: parent.left; right: parent.right; top: titleBar.bottom; bottom: parent.bottom }
        spacing: 0

        Sidebar {
            visible: !win.playerFullscreen
            Layout.preferredWidth: 76
            Layout.fillHeight: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

        // Aviso do Deadlock: instalado e sem -condebug, só a sessão inteira pode ser gravada.
        Rectangle {
            readonly property var dl: App.deadlock || ({})
            visible: !win.current && !win.playerFullscreen && dl.installed === true
                     && dl.condebug !== true && dl.dismissed !== true
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 14
            implicitHeight: tipRow.implicitHeight + 20
            radius: 10
            color: Theme.card
            border.color: Qt.darker(Theme.multi, 1.6)
            RowLayout {
                id: tipRow
                anchors.fill: parent
                anchors.margins: 10
                anchors.leftMargin: 14
                spacing: 12
                Icon { name: "info"; size: 18; color: Theme.multi }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    color: Theme.text
                    font.pixelSize: 13
                    text: "<b>Deadlock:</b> para gravar cada partida do início ao fim (e não a sessão inteira), "
                        + "adicione <b>-condebug</b> nas opções de inicialização do jogo na Steam."
                }
                AButton { primary: true; text: "Como ativar"; onClicked: win.openDeadlockTip() }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Loader {
                anchors.fill: parent
                // Com um clipe aberto por cima, a tela de baixo sai (o player da sessão não continua tocando).
                active: !win.current
                sourceComponent: win.view === "settings" ? settingsPage : win.view === "about" ? aboutPage : win.view === "clips" ? clipsPage : sessionsPage
            }
            Loader {
                anchors.fill: parent
                active: win.current !== null
                sourceComponent: Component { Player { media: win.current } }
            }
        }
        }
    }

    DeadlockDialog { id: deadlockDialog }

    Component { id: sessionsPage; SessionsView {} }
    Component { id: clipsPage; ClipsView {} }
    Component { id: settingsPage; SettingsView {} }
    Component { id: aboutPage; AboutView {} }

    // Contorno da janela (sem a moldura do Windows), só quando ela não ocupa a tela.
    Rectangle {
        anchors.fill: parent
        visible: win.visibility === Window.Windowed
        color: "transparent"
        border.color: Theme.line
        z: 200
    }

    Toasts {
        id: toasts
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        z: 100
    }

    // Exclusão com confirmação (vai para a lixeira).
    Dialog {
        id: deleteDialog
        property var target: null
        anchors.centerIn: parent
        modal: true
        title: "Excluir vídeo?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: 380
            wrapMode: Text.Wrap
            text: deleteDialog.target ? "\"" + deleteDialog.target.label + "\" será movido para a lixeira." : ""
        }
        onAccepted: {
            const path = target.path
            win.deletingPath = path // o player da sessão solta o arquivo
            win.current = null      // e o de clipes fecha
            Qt.callLater(() => App.deleteClips([path]))
        }
    }
    // Depuração (--open player): abre o vídeo mais recente assim que a biblioteca carregar.
    function openDebugPlayer() {
        const all = App.library.matches || []
        if (!debugPlayer || openedPlayer || all.length === 0)
            return false
        openedPlayer = true
        current = all[0].clips[0]
        return true
    }
    Component.onCompleted: {
        if (InitialPage.startsWith("match:") || InitialPage === "deadlock") go(InitialPage)
        else openDebugPlayer()
    }
    function toastsShow(text, kind) { toasts.show(text, kind) }
    function openDeadlockTip() { deadlockDialog.open() }
    function confirmDelete(c) {
        deleteDialog.target = c
        deleteDialog.open()
    }
}
