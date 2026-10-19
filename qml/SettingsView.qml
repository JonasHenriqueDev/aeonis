import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Configurações geradas a partir do esquema (SettingsSchema.cpp), agrupadas por categoria,
// com a barra "alterações não salvas". Inclui o armazenamento; versão e atualizações ficam em Sobre.
Item {
    id: page
    property var dirty: ({})
    readonly property int dirtyCount: Object.keys(dirty).length
    readonly property var categories: {
        const out = []
        for (const f of App.settingsFields) {
            let c = out.find(x => x.name === f.category)
            if (!c) out.push(c = { name: f.category, fields: [] })
            c.fields.push(f)
        }
        return out
    }
    function valueOf(f) { return f.key in dirty ? dirty[f.key] : f.value }
    function fieldOf(key) { return App.settingsFields.find(f => f.key === key) }
    function valueOfKey(key) { const f = fieldOf(key); return f ? valueOf(f) : undefined }
    function same(a, b) { return JSON.stringify(a) === JSON.stringify(b) }
    function mark(f, v) {
        const d = Object.assign({}, dirty)
        if (v === f.value || same(v, f.value)) delete d[f.key]
        else d[f.key] = v
        dirty = d
    }

    Connections {
        target: App
        function onSettingsChanged() { page.dirty = ({}) }
    }

    PageHeader {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28 }
        title: "Configurações"
        subtitle: "Gravação, clipes, áudio e atalhos"
        actions: false
    }

    SmoothScroll {
        id: scroll
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: saveBar.visible ? saveBar.top : parent.bottom; topMargin: 16 }
        contentHeight: content.implicitHeight + 28

        ColumnLayout {
            id: content
            width: Math.min(scroll.width - 56, 980)
            x: 28
            spacing: 16

            Repeater {
                model: page.categories
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: col.implicitHeight + 28
                    radius: Theme.radius
                    color: Theme.panel
                    border.color: Theme.line
                    ColumnLayout {
                        id: col
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18; topMargin: 14 }
                        spacing: 0
                        Text { text: card.modelData.name; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.bottomMargin: 6 }
                        Repeater {
                            model: card.modelData.fields
                            delegate: ColumnLayout {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 0
                                Rectangle { visible: index > 0; Layout.fillWidth: true; height: 1; color: Theme.line }
                                SettingRow {
                                    Layout.fillWidth: true
                                    field: modelData
                                    value: page.valueOf(modelData)
                                    host: page
                                    onEdited: v => page.mark(modelData, v)
                                    onEditedKey: (key, v) => { const f = page.fieldOf(key); if (f) page.mark(f, v) }
                                }
                            }
                        }
                    }
                }
            }

            // Armazenamento
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: storage.implicitHeight + 32
                radius: Theme.radius
                color: Theme.panel
                border.color: Theme.line
                ColumnLayout {
                    id: storage
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
                    spacing: 8
                    Text { text: "Armazenamento"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font.pixelSize: 13
                        text: "Vídeos: " + F.size(App.library.totalBytes || 0) + " no total, "
                              + F.size(App.library.unsavedBytes || 0) + " não salvos (contam para o limite)."
                    }
                    RowLayout {
                        AButton { text: "Abrir pasta dos vídeos"; iconName: "folder"; onClicked: App.openFolder() }
                        AButton { text: "Abrir log"; iconName: "external"; onClicked: App.openLog() }
                    }
                }
            }
        }
    }

    Rectangle {
        id: saveBar
        visible: page.dirtyCount > 0
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 60
        color: Theme.side
        Rectangle { width: parent.width; height: 1; color: Theme.line }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            Text {
                Layout.fillWidth: true
                color: Theme.text
                text: page.dirtyCount === 1 ? "1 alteração não salva" : page.dirtyCount + " alterações não salvas"
            }
            AButton { text: "Descartar"; onClicked: page.dirty = ({}) }
            AButton { text: "Salvar alterações"; primary: true; onClicked: App.saveSettings(page.dirty) }
        }
    }
}
