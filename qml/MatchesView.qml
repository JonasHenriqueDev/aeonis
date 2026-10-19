import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Partidas agrupadas por dia (Hoje, Ontem, ...), cada uma com seus clipes.
Item {
    readonly property var list: win.matches
    readonly property int clipCount: list.reduce((n, m) => n + m.clips.length, 0)

    PageHeader {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28; bottomMargin: 0 }
        title: win.gameFilter ? F.gameName(win.gameFilter) : "Partidas"
        subtitle: list.length ? F.plural(list.length, "partida", "partidas") + " · " + F.plural(clipCount, "vídeo", "vídeos")
                                + " · " + F.size(win.lib.totalBytes || 0) + " no disco" : ""
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
        spacing: 14
        visible: list.length > 0
        model: list
        cacheBuffer: 600
        ScrollBar.vertical: ScrollBar {}
        delegate: ColumnLayout {
            id: row
            required property var modelData
            required property int index
            width: listView.width - 56
            spacing: 10
            readonly property string day: F.dayLabel(modelData.start)
            readonly property bool firstOfDay: index === 0 || F.dayLabel(list[index - 1].start) !== day
            readonly property int highlights: modelData.clips.filter(c => c.kind === "highlight").length
            readonly property bool hasFull: modelData.clips.some(c => c.kind === "full")

            RowLayout {
                visible: row.firstOfDay
                Layout.topMargin: row.index === 0 ? 0 : 14
                spacing: 10
                Text { text: row.day; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text {
                    text: F.plural(list.filter(m => F.dayLabel(m.start) === row.day).length, "partida", "partidas")
                    color: Theme.faint
                    font.pixelSize: 13
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: card.implicitHeight + 32
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.line
                ColumnLayout {
                    id: card
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                    spacing: 14
                    RowLayout {
                        spacing: 12
                        GameAvatar { game: row.modelData.game; iconUrl: win.gameIcon(row.modelData.game); size: 40 }
                        ColumnLayout {
                            spacing: 2
                            Text { text: F.gameName(row.modelData.game); color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text {
                                text: F.time(row.modelData.start) + " – " + F.time(row.modelData.end) + " · " + F.span(row.modelData.start, row.modelData.end)
                                color: Theme.muted
                                font.pixelSize: 12
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Badge { visible: row.highlights > 0; text: F.plural(row.highlights, "destaque", "destaques"); tint: Theme.multi }
                        Badge { visible: row.hasFull; text: "Partida completa"; tint: Theme.full }
                        Badge { text: F.plural(row.modelData.clips.length, "vídeo", "vídeos"); tint: Theme.faint }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 12
                        Repeater {
                            model: row.modelData.clips
                            ClipTile { required property var modelData; item: modelData }
                        }
                    }
                }
            }
        }
    }

    component Badge: Rectangle {
        property alias text: t.text
        property color tint: Theme.faint
        radius: 6
        height: 24
        width: t.implicitWidth + 16
        color: Qt.rgba(tint.r, tint.g, tint.b, 0.16)
        Text { id: t; anchors.centerIn: parent; color: Qt.lighter(parent.tint, 1.3); font.pixelSize: 12 }
    }
}
