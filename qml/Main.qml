import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Janela principal: barra lateral (abas, jogos, status) + partidas / clipes / configurações,
// com o player por cima. Criada ao abrir e destruída ao fechar (WindowHost).
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

    property string view: ["settings", "about", "clips"].includes(InitialPage) ? InitialPage : "matches"
    property string gameFilter: ""
    property var current: null        // clipe aberto no player
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
    readonly property var matches: (lib.matches || []).filter(m => !gameFilter || m.game === gameFilter)

    function go(v) {
        if (v === "player") { debugPlayer = true; openDebugPlayer(); return }
        if (v.startsWith("match:")) {
            view = "matches"
            gameFilter = ""
            pendingMatch = v.slice(6)
            openPendingMatch()
            return
        }
        if (v === "library") v = "matches"
        current = null
        view = v
    }
    function openClip(c) { current = c }
    function openMatch(m) { const c = F.matchMain(m); if (c) current = c }
    // Abre `c` (ex.: a partida completa) já na posição `t` segundos.
    function openAt(c, t) { pendingSeek = t; current = c }
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
    function gameIcon(name) {
        const g = (lib.games || []).find(x => x.name === name)
        return g ? g.icon : ""
    }
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
            if (win.openDebugPlayer() || win.openPendingMatch())
                return
            if (win.current) {
                const c = win.findClip(win.current.id)
                win.current = c
            }
            if (win.gameFilter && !(App.library.games || []).some(g => g.name === win.gameFilter))
                win.gameFilter = ""
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
            Layout.preferredWidth: 248
            Layout.fillHeight: true
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Loader {
                anchors.fill: parent
                visible: !win.current
                sourceComponent: win.view === "settings" ? settingsPage : win.view === "about" ? aboutPage : win.view === "clips" ? clipsPage : matchesPage
            }
            Loader {
                anchors.fill: parent
                active: win.current !== null
                sourceComponent: Component { Player { media: win.current } }
            }
        }
    }

    Component { id: matchesPage; MatchesView {} }
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
            win.current = null // libera o arquivo antes de excluir
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
        if (InitialPage.startsWith("match:")) go(InitialPage)
        else openDebugPlayer()
    }
    function toastsShow(text, kind) { toasts.show(text, kind) }
    function confirmDelete(c) {
        deleteDialog.target = c
        deleteDialog.open()
    }
}
