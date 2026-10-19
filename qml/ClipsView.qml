import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Clipes (destaques, manuais e cortes), do mais novo para o mais antigo, agrupados por dia.
// As partidas completas ficam na aba Partidas.
Item {
    readonly property var clips: {
        const all = []
        for (const m of win.matches)
            for (const c of m.clips) if (c.kind !== "full") all.push(c)
        all.sort((a, b) => new Date(b.created) - new Date(a.created))
        return all
    }
    readonly property var days: {
        const out = []
        for (const c of clips) {
            const d = F.dayLabel(c.created)
            if (!out.length || out[out.length - 1].day !== d) out.push({ day: d, clips: [] })
            out[out.length - 1].clips.push(c)
        }
        return out
    }

    PageHeader {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28 }
        title: win.gameFilter ? "Clipes · " + F.gameName(win.gameFilter) : "Clipes"
        subtitle: clips.length ? F.plural(clips.length, "clipe", "clipes") : ""
    }

    EmptyState { anchors.centerIn: parent; visible: clips.length === 0 }

    ListView {
        id: listView
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 16 }
        leftMargin: 28
        rightMargin: 28
        bottomMargin: 28
        clip: true
        spacing: 22
        model: days
        cacheBuffer: 800
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        FastWheel {}
        delegate: ColumnLayout {
            id: group
            required property var modelData
            width: listView.width - 56
            spacing: 12
            Text { text: group.modelData.day; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Flow {
                Layout.fillWidth: true
                spacing: 14
                Repeater {
                    model: group.modelData.clips
                    ClipTile { required property var modelData; item: modelData; showGame: true }
                }
            }
        }
    }
}
