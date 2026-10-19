import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Partidas agrupadas por dia (Hoje, Ontem, ...): uma linha por partida com campeão, KDA, modo,
// resultado e os eventos. Clicar abre a partida no player; os clipes ficam na aba Clipes.
Item {
    readonly property var list: win.matches

    PageHeader {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28; bottomMargin: 0 }
        title: win.gameFilter ? F.gameName(win.gameFilter) : "Partidas"
        subtitle: list.length ? F.plural(list.length, "partida", "partidas") + " · " + F.size(win.lib.totalBytes || 0) + " no disco" : ""
    }

    EmptyState {
        anchors.centerIn: parent
        visible: list.length === 0
    }

    ListView {
        id: listView
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 16 }
        leftMargin: 28
        rightMargin: 28
        bottomMargin: 28
        clip: true
        spacing: 10
        visible: list.length > 0
        model: list
        cacheBuffer: 800
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        FastWheel {}
        delegate: ColumnLayout {
            id: row
            required property var modelData
            required property int index
            width: listView.width - 56
            spacing: 10
            readonly property string day: F.dayLabel(modelData.start)
            readonly property bool firstOfDay: index === 0 || F.dayLabel(list[index - 1].start) !== day

            RowLayout {
                visible: row.firstOfDay
                Layout.topMargin: row.index === 0 ? 0 : 16
                spacing: 10
                Text { text: row.day; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text {
                    text: F.plural(list.filter(m => F.dayLabel(m.start) === row.day).length, "partida", "partidas")
                    color: Theme.faint
                    font.pixelSize: 13
                }
            }

            MatchCard { Layout.fillWidth: true; match: row.modelData }
        }
    }

    component MatchCard: AbstractButton {
        id: card
        property var match
        readonly property var info: match.info || {}
        readonly property bool hasInfo: info.champion !== undefined
        readonly property var main: F.matchMain(match)
        readonly property var summary: F.eventSummary(match.events)
        readonly property int clipCount: match.clips.filter(c => c.kind !== "full").length
        readonly property color resultColor: info.result === "Win" ? Theme.assist : info.result === "Lose" ? Theme.kill : Theme.line
        implicitHeight: 100
        padding: 10
        leftPadding: 14
        hoverEnabled: true
        onClicked: win.openMatch(match)

        background: Rectangle {
            radius: Theme.radius
            color: card.hovered ? Theme.card : Theme.panel
            border.color: card.hovered ? Theme.accent : Theme.line
            Rectangle { // faixa do resultado, como no Outplayed
                visible: card.info.result !== undefined
                x: 1; y: 12; width: 3; height: parent.height - 24; radius: 1.5
                color: card.resultColor
            }
        }

        contentItem: RowLayout {
            spacing: 14

            // Miniatura do vídeo principal (partida completa ou primeiro clipe)
            Rectangle {
                Layout.preferredWidth: 142
                Layout.preferredHeight: 80
                radius: 8
                color: Theme.card
                clip: true
                Image {
                    anchors.fill: parent
                    source: card.main ? win.thumbOf(card.main) : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 320
                }
                Rectangle {
                    anchors { right: parent.right; bottom: parent.bottom; margins: 6 }
                    visible: !!card.main
                    radius: 5; height: 18; width: dur.implicitWidth + 10
                    color: "#cc000000"
                    Text { id: dur; anchors.centerIn: parent; text: card.main ? F.dur(win.durationOf(card.main)) : ""; color: "white"; font.pixelSize: 11 }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 36; height: 36; radius: 18
                    color: "#99000000"
                    visible: card.hovered
                    Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 1; name: "play"; size: 16; color: "white" }
                }
            }

            // Campeão (LoL) ou ícone do jogo
            MatchAvatar { match: card.match; size: 52 }

            ColumnLayout {
                Layout.preferredWidth: 220
                Layout.minimumWidth: 190
                Layout.maximumWidth: 260
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: card.hasInfo ? card.info.champion : F.gameName(card.match.game)
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: card.hasInfo ? F.modeText(card.info) || F.gameName(card.match.game)
                                       : F.time(card.match.start) + " – " + F.time(card.match.end)
                    color: Theme.muted
                    font.pixelSize: 12
                }
                Text {
                    text: F.time(card.match.start) + " · " + (card.info.gameTime ? F.dur(card.info.gameTime) + " de jogo" : F.span(card.match.start, card.match.end))
                    visible: card.hasInfo
                    color: Theme.faint
                    font.pixelSize: 12
                }
            }

            // KDA
            ColumnLayout {
                visible: card.hasInfo
                Layout.preferredWidth: 112
                spacing: 2
                Row {
                    spacing: 0
                    Text { text: card.info.kills !== undefined ? card.info.kills : ""; color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text { text: " / "; color: Theme.faint; font.pixelSize: 18 }
                    Text { text: card.info.deaths !== undefined ? card.info.deaths : ""; color: Theme.kill; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text { text: " / "; color: Theme.faint; font.pixelSize: 18 }
                    Text { text: card.info.assists !== undefined ? card.info.assists : ""; color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                }
                Text {
                    text: F.kdaRatio(card.info) + (card.info.cs !== undefined ? " · " + card.info.cs + " CS" : "")
                    color: Theme.muted
                    font.pixelSize: 12
                }
            }

            // Resultado
            Rectangle {
                visible: F.resultText(card.info) !== ""
                radius: 6
                implicitHeight: 26
                implicitWidth: resultLabel.implicitWidth + 18
                color: Qt.rgba(card.resultColor.r, card.resultColor.g, card.resultColor.b, 0.16)
                Text { id: resultLabel; anchors.centerIn: parent; text: F.resultText(card.info); color: Qt.lighter(card.resultColor, 1.25); font.pixelSize: 12; font.weight: Font.DemiBold }
            }

            Item { Layout.fillWidth: true }

            // Eventos da partida (ícones com contagem)
            Row {
                spacing: 7
                Repeater {
                    model: card.summary.slice(0, 8)
                    Row {
                        required property var modelData
                        spacing: 4
                        EventBadge { label: modelData.label; size: 22; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: modelData.count; color: Theme.muted; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                        ToolTip.visible: hover.hovered
                        ToolTip.text: modelData.label + " · " + modelData.count
                        HoverHandler { id: hover }
                    }
                }
            }

            ColumnLayout {
                spacing: 2
                Layout.preferredWidth: 72
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: card.clipCount ? F.plural(card.clipCount, "clipe", "clipes") : "sem clipes"
                    color: Theme.muted
                    font.pixelSize: 12
                }
                Text {
                    Layout.alignment: Qt.AlignRight
                    visible: card.match.clips.some(c => c.kind === "full")
                    text: "partida completa"
                    color: Theme.faint
                    font.pixelSize: 11
                }
            }
            Icon { name: "back"; rotation: 180; size: 16; color: Theme.faint }
        }
    }
}
