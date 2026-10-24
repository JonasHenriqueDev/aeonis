import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Tela inicial, no estilo do Outplayed: "Sessões › jogo · dia", a faixa com as sessões (partidas) do
// dia e, embaixo, o player da sessão escolhida com a linha do tempo e o filtro de eventos.
Item {
    id: sv
    readonly property var all: App.library.matches || []

    // Jogos com sessões, do mais recente para o mais antigo.
    readonly property var games: {
        const out = []
        for (const m of all) {
            let g = out.find(x => x.name === m.game)
            if (!g) out.push(g = { name: m.game, count: 0, last: m.start })
            g.count++
            if (new Date(m.start) > new Date(g.last)) g.last = m.start
        }
        return out.sort((a, b) => new Date(b.last) - new Date(a.last))
    }
    readonly property string game: games.some(g => g.name === win.selGame) ? win.selGame : (games.length ? games[0].name : "")
    // Dias do jogo, do mais recente para o mais antigo.
    readonly property var days: {
        const out = []
        for (const m of all) {
            if (m.game !== game) continue
            const k = F.dayKey(m.start)
            const d = out.find(x => x.key === k)
            if (d) d.count++
            else out.push({ key: k, date: m.start, count: 0 + 1 })
        }
        out.sort((a, b) => b.key.localeCompare(a.key))
        return out.map(d => ({ key: d.key, label: F.dayLabel(d.date), count: d.count }))
    }
    readonly property string day: days.some(d => d.key === win.selDay) ? win.selDay : (days.length ? days[0].key : "")
    // Sessões do dia em ordem cronológica: Sessão 01, 02...
    readonly property var sessions: all.filter(m => m.game === game && F.dayKey(m.start) === day)
                                       .sort((a, b) => new Date(a.start) - new Date(b.start))
    readonly property int index: {
        const i = sessions.findIndex(m => m.id === win.selSession)
        return i >= 0 ? i : sessions.length - 1
    }
    readonly property var session: index >= 0 ? sessions[index] : null
    readonly property var info: session && session.info ? session.info : ({})
    readonly property var media: {
        if (!session) return null
        return session.clips.find(c => c.id === win.selVideo) || F.matchMain(session)
    }
    readonly property bool fullscreen: win.playerFullscreen

    function sessionName(i) { return "Sessão " + F.two(i + 1) }
    function pick(m) { win.selSession = m.id; win.selVideo = "" }

    EmptyState {
        anchors.centerIn: parent
        visible: sv.all.length === 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: sv.fullscreen ? 0 : 20
        anchors.topMargin: sv.fullscreen ? 0 : 16
        spacing: 12
        visible: sv.all.length > 0

        // Sessões › Jogo · Dia
        RowLayout {
            visible: !sv.fullscreen
            Layout.fillWidth: true
            spacing: 6

            Crumb {
                iconName: "grid"
                text: "Sessões"
                onClicked: gameGrid.open()
                ToolTip.visible: hovered
                ToolTip.text: "Escolher o jogo"
            }
            Icon { name: "back"; rotation: 180; size: 14; color: Theme.faint }
            Crumb {
                game: sv.game
                text: F.gameName(sv.game)
                chevron: true
                onClicked: gameGrid.open()
            }
            Item { width: 6 }
            Crumb {
                iconName: "calendar"
                text: {
                    const d = sv.days.find(x => x.key === sv.day)
                    return d ? d.label : ""
                }
                chevron: true
                onClicked: dayMenu.open()
                Menu {
                    id: dayMenu
                    y: parent.height + 4
                    width: 260
                    Repeater {
                        model: sv.days
                        MenuItem {
                            required property var modelData
                            text: modelData.label + "  ·  " + F.plural(modelData.count, "sessão", "sessões")
                            checkable: true
                            checked: modelData.key === sv.day
                            onTriggered: { win.selDay = modelData.key; win.selSession = ""; win.selVideo = "" }
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: F.plural(sv.sessions.length, "sessão", "sessões") + " neste dia"
                color: Theme.faint
                font.pixelSize: 12
            }
        }

        // Faixa de sessões do dia, da mais nova (à esquerda) para a mais antiga. A ordem é invertida no
        // modelo: com layoutDirection RightToLeft o Qt posiciona a rolagem de forma inconsistente (a
        // faixa abria no meio).
        ListView {
            id: strip
            readonly property int selected: sv.index >= 0 ? sv.sessions.length - 1 - sv.index : -1
            function reposition() {
                if (selected >= 0) positionViewAtIndex(selected, ListView.Contain)
                else positionViewAtBeginning()
            }
            visible: !sv.fullscreen
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            orientation: ListView.Horizontal
            spacing: 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: sv.sessions.slice().reverse()
            currentIndex: selected
            onModelChanged: Qt.callLater(reposition)
            onCurrentIndexChanged: reposition()
            onWidthChanged: Qt.callLater(reposition)
            FastWheel { horizontal: true }
            delegate: AbstractButton {
                id: card
                required property var modelData
                required property int index
                readonly property int number: sv.sessions.length - 1 - index // posição cronológica
                readonly property var info: modelData.info || {}
                readonly property var main: F.matchMain(modelData)
                readonly property bool on: index === strip.selected
                width: 228
                height: 64
                hoverEnabled: true
                onClicked: sv.pick(modelData)
                background: Rectangle {
                    radius: 10
                    color: card.on ? Theme.cardHi : card.hovered ? Theme.card : Theme.panel
                    border.color: card.on ? Theme.accent : Theme.line
                    border.width: card.on ? 1.5 : 1
                }
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        Layout.leftMargin: 6
                        Layout.preferredWidth: 86
                        Layout.preferredHeight: 50
                        radius: 6
                        color: Theme.card
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: card.main ? win.thumbOf(card.main) : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 180
                        }
                        Rectangle {
                            anchors { right: parent.right; bottom: parent.bottom; margins: 3 }
                            visible: !!card.main
                            radius: 4; height: 14; width: cardDur.implicitWidth + 8
                            color: "#cc000000"
                            Text { id: cardDur; anchors.centerIn: parent; text: card.main ? F.dur(win.durationOf(card.main)) : ""; color: "white"; font.pixelSize: 9 }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: 8
                        spacing: 2
                        Text {
                            text: sv.sessionName(card.number)
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        RowLayout {
                            spacing: 5
                            MatchAvatar { visible: card.info.champion !== undefined; match: card.modelData; size: 16 }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: card.info.kills !== undefined ? F.kda(card.info) : "Vídeos: " + card.modelData.clips.length
                                color: card.info.result === "Win" ? Theme.assist : card.info.result === "Lose" ? Theme.kill : Theme.muted
                                font.pixelSize: 11
                            }
                        }
                        Text { text: F.time(card.modelData.start); color: Theme.faint; font.pixelSize: 10 }
                    }
                }
            }
        }

        // Sem partida completa (ou ela dividida em partes): os vídeos da sessão para escolher.
        Row {
            visible: !sv.fullscreen && !!sv.session && sv.session.clips.length > 1
                     && sv.session.clips.filter(c => c.kind === "full").length !== 1 // sem partida completa, ou ela em partes de 1 h
            spacing: 6
            Text { text: "Vídeos da sessão:"; color: Theme.muted; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            Repeater {
                model: sv.session ? sv.session.clips : []
                AButton {
                    required property var modelData
                    flat: !(sv.media && sv.media.id === modelData.id)
                    implicitHeight: 28
                    text: F.chipText(modelData)
                    iconName: F.clipIcon(modelData)
                    onClicked: win.selVideo = modelData.id
                }
            }
        }

        Player {
            Layout.fillWidth: true
            Layout.fillHeight: true
            embedded: true
            media: sv.media
            title: sv.session ? sv.sessionName(sv.index) + (sv.info.champion ? " · " + sv.info.champion + " · " + F.kda(sv.info) : "") : ""
            subtitle: sv.session ? [F.modeText(sv.info), F.resultText(sv.info),
                                    F.dayLabel(sv.session.start) + ", " + F.time(sv.session.start) + " – " + F.time(sv.session.end),
                                    sv.info.gameTime ? F.dur(sv.info.gameTime) + " de jogo" : ""].filter(x => x).join(" · ") : ""
        }
    }

    GameGrid {
        id: gameGrid
        games: sv.games
        current: sv.game
        onChosen: name => { win.selGame = name; win.selDay = ""; win.selSession = ""; win.selVideo = "" }
    }

    // Item da trilha "Sessões › Jogo · Dia": texto com ícone (ou avatar do jogo) e seta opcional.
    component Crumb: AbstractButton {
        id: crumb
        property string iconName: ""
        property string game: ""
        property bool chevron: false
        implicitHeight: 34
        implicitWidth: crumbRow.implicitWidth + 20
        hoverEnabled: true
        background: Rectangle { radius: 8; color: crumb.hovered || crumb.down ? Theme.card : "transparent" }
        contentItem: Item {
            RowLayout {
                id: crumbRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Icon { visible: crumb.iconName !== ""; name: crumb.iconName; size: 16; color: Theme.muted }
                GameAvatar { visible: crumb.game !== ""; game: crumb.game; iconUrl: win.gameIcon(crumb.game); size: 22 }
                Text { text: crumb.text; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                Icon { visible: crumb.chevron; name: "back"; rotation: -90; size: 12; color: Theme.muted }
            }
        }
    }
}
