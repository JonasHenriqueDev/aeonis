import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Clipes (destaques, manuais e cortes) divididos por partida e por dia: cada bloco diz de qual
// partida os clipes vieram (campeão, KDA, modo, resultado, data). As partidas completas ficam na aba Partidas.
Item {
    // Partidas que têm clipes, da mais nova para a mais antiga, com só os clipes (mais recentes primeiro).
    readonly property var groups: {
        const out = []
        for (const m of win.matches) {
            const clips = m.clips.filter(c => c.kind !== "full")
            if (!clips.length) continue
            clips.sort((a, b) => new Date(b.created) - new Date(a.created))
            out.push({ match: m, clips: clips })
        }
        return out
    }
    readonly property int clipCount: groups.reduce((n, g) => n + g.clips.length, 0)

    PageHeader {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28 }
        title: win.gameFilter ? "Clipes · " + F.gameName(win.gameFilter) : "Clipes"
        subtitle: clipCount ? F.plural(clipCount, "clipe", "clipes") + " · " + F.plural(groups.length, "partida", "partidas") : ""
    }

    EmptyState { anchors.centerIn: parent; visible: clipCount === 0 }

    ListView {
        id: listView
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 16 }
        leftMargin: 28
        rightMargin: 28
        bottomMargin: 28
        clip: true
        spacing: 14
        model: groups
        cacheBuffer: 800
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        FastWheel {}
        delegate: ColumnLayout {
            id: group
            required property var modelData
            required property int index
            readonly property var match: modelData.match
            readonly property var info: match.info || {}
            readonly property string day: F.dayLabel(match.start)
            readonly property bool firstOfDay: index === 0 || F.dayLabel(groups[index - 1].match.start) !== day
            width: listView.width - 56
            spacing: 12

            Text {
                visible: group.firstOfDay
                Layout.topMargin: group.index === 0 ? 0 : 14
                text: group.day
                color: Theme.text
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: body.implicitHeight + 28
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.line

                ColumnLayout {
                    id: body
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                    spacing: 12

                    // De qual partida são estes clipes
                    AbstractButton {
                        id: matchHead
                        Layout.fillWidth: true
                        implicitHeight: 44
                        hoverEnabled: true
                        onClicked: win.openMatch(group.match)
                        ToolTip.visible: hovered
                        ToolTip.text: "Abrir a partida"
                        background: Item {}
                        contentItem: RowLayout {
                            spacing: 12
                            MatchAvatar { match: group.match; size: 40 }
                            ColumnLayout {
                                spacing: 2
                                Text {
                                    text: group.info.champion ? group.info.champion + " · " + F.gameName(group.match.game) : F.gameName(group.match.game)
                                    color: matchHead.hovered ? Theme.accent : Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: [F.modeText(group.info),
                                           F.dayLabel(group.match.start) + ", " + F.time(group.match.start) + " – " + F.time(group.match.end),
                                           group.info.gameTime ? F.dur(group.info.gameTime) + " de jogo" : ""].filter(x => x).join(" · ")
                                    color: Theme.muted
                                    font.pixelSize: 12
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Row {
                                visible: group.info.kills !== undefined
                                spacing: 0
                                Text { text: group.info.kills !== undefined ? group.info.kills : ""; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                                Text { text: " / "; color: Theme.faint; font.pixelSize: 16 }
                                Text { text: group.info.deaths !== undefined ? group.info.deaths : ""; color: Theme.kill; font.pixelSize: 16; font.weight: Font.DemiBold }
                                Text { text: " / "; color: Theme.faint; font.pixelSize: 16 }
                                Text { text: group.info.assists !== undefined ? group.info.assists : ""; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                            }
                            Rectangle {
                                readonly property color tint: group.info.result === "Win" ? Theme.assist : Theme.kill
                                visible: F.resultText(group.info) !== ""
                                radius: 6
                                implicitHeight: 24
                                implicitWidth: res.implicitWidth + 16
                                color: Qt.rgba(tint.r, tint.g, tint.b, 0.16)
                                Text { id: res; anchors.centerIn: parent; text: F.resultText(group.info); color: Qt.lighter(parent.tint, 1.25); font.pixelSize: 12; font.weight: Font.DemiBold }
                            }
                            Text { text: F.plural(group.modelData.clips.length, "clipe", "clipes"); color: Theme.faint; font.pixelSize: 12 }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 14
                        Repeater {
                            model: group.modelData.clips
                            ClipTile { required property var modelData; item: modelData }
                        }
                    }
                }
            }
        }
    }
}
