import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "Format.js" as F

// Uma linha das configurações: rótulo + descrição à esquerda, controle conforme o tipo à direita.
Item {
    id: row
    property var field
    property var value
    // Valor atual (com alterações não salvas) de outra chave e edição dela: os jogos escolhidos e os
    // personalizados mexem um no outro.
    property var host: null // SettingsView (valueOfKey)
    function lookup(key) { return host ? host.valueOfKey(key) : undefined }
    signal edited(var value)
    signal editedKey(string key, var value)
    // Listas de jogos ocupam a linha toda, abaixo do rótulo.
    readonly property bool wide: field.type === "games" || field.type === "customgames"
    implicitHeight: wide ? labels.implicitHeight + control.implicitHeight + 38
                         : Math.max(labels.implicitHeight, control.implicitHeight) + 20

    // "Nome=exe1.exe;exe2.exe" por linha -> [{ name, exes, line }]
    function parseCustom(text) {
        const out = []
        for (const raw of (text || "").split("\n")) {
            const line = raw.trim()
            if (!line || line.startsWith("#")) continue
            const eq = line.indexOf("=")
            const name = eq > 0 ? line.slice(0, eq).trim() : line
            const exes = (eq > 0 ? line.slice(eq + 1) : line).split(";").map(e => e.trim()).filter(e => e)
            if (exes.length) out.push({ name: name, exes: exes, line: line })
        }
        return out
    }
    function customText() { const v = lookup("CustomGames"); return v === undefined ? "" : v }
    // Acrescenta um jogo escolhido no Explorer aos personalizados (se ainda não estiver lá).
    function addCustom(g) {
        const text = customText()
        const known = parseCustom(text)
        const exe = g.process + ".exe"
        if (known.some(c => c.exes.some(e => e.toLowerCase() === exe.toLowerCase())))
            return known.find(c => c.exes.some(e => e.toLowerCase() === exe.toLowerCase())).name
        let name = g.name
        while (known.some(c => c.name.toLowerCase() === name.toLowerCase())) name += " (2)"
        const next = (text.trim() ? text.trim() + "\n" : "") + name + "=" + exe
        if (field.key === "CustomGames") edited(next)
        else editedKey("CustomGames", next)
        return name
    }

    ColumnLayout {
        id: labels
        anchors.left: parent.left
        anchors.right: row.wide ? parent.right : control.left
        anchors.rightMargin: row.wide ? 0 : 24
        anchors.verticalCenter: row.wide ? undefined : parent.verticalCenter
        anchors.top: row.wide ? parent.top : undefined
        anchors.topMargin: 12
        spacing: 3
        Text { text: row.field.label; color: Theme.text; font.pixelSize: 14 }
        Text {
            visible: row.field.description !== ""
            Layout.fillWidth: true
            text: row.field.description
            color: Theme.muted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }

    Loader {
        id: control
        anchors.right: parent.right
        anchors.left: row.wide ? parent.left : undefined
        anchors.verticalCenter: row.wide ? undefined : parent.verticalCenter
        anchors.top: row.wide ? labels.bottom : undefined
        anchors.topMargin: 12
        sourceComponent: {
            switch (row.field.type) {
            case "bool": return boolControl
            case "select": return selectControl
            case "preset": return presetControl
            case "int": case "number": return numberControl
            case "hotkey": return hotkeyControl
            case "folder": return folderControl
            case "multiline": return multilineControl
            case "games": return gamesControl
            case "customgames": return customGamesControl
            default: return textControl
            }
        }
    }

    Component {
        id: boolControl
        Switch { checked: row.value === true; onToggled: row.edited(checked) }
    }
    Component {
        id: selectControl
        ComboBox {
            implicitWidth: 300
            model: row.field.options
            textRole: "label"
            valueRole: "value"
            currentIndex: row.field.options.findIndex(o => o.value === row.value)
            onActivated: index => row.edited(row.field.options[index].value)
        }
    }
    // Predefinições + "Personalizada" (valor livre em Mbps).
    Component {
        id: presetControl
        RowLayout {
            id: preset
            spacing: 8
            readonly property var options: row.field.options.concat([{ value: -1, label: "Personalizada" }])
            property bool custom: !row.field.options.some(o => o.value === row.value)
            ComboBox {
                implicitWidth: 240
                model: preset.options
                textRole: "label"
                valueRole: "value"
                currentIndex: preset.custom ? preset.options.length - 1 : preset.options.findIndex(o => o.value === row.value)
                onActivated: index => {
                    const v = preset.options[index].value
                    preset.custom = v === -1
                    if (v !== -1) row.edited(v)
                }
            }
            LiveField {
                visible: preset.custom
                implicitWidth: 70
                horizontalAlignment: Text.AlignRight
                validator: IntValidator { bottom: 2; top: 150 }
                source: String(row.value)
                onTyped: t => { const v = parseInt(t); if (!isNaN(v)) row.edited(v) }
            }
            Text { visible: preset.custom; text: "Mbps"; color: Theme.muted; font.pixelSize: 13 }
        }
    }
    Component {
        id: numberControl
        LiveField {
            implicitWidth: 140
            horizontalAlignment: Text.AlignRight
            validator: row.field.type === "int" ? intValidator : doubleValidator
            source: String(row.value)
            onTyped: t => {
                const v = row.field.type === "int" ? parseInt(t) : parseFloat(t.replace(",", "."))
                if (!isNaN(v)) row.edited(v)
            }
            IntValidator { id: intValidator; bottom: 0; top: 100000 }
            DoubleValidator { id: doubleValidator; bottom: 0; top: 100000; notation: DoubleValidator.StandardNotation }
        }
    }
    // Campo que avisa a alteração a cada tecla (a barra "Salvar" aparece na hora) sem que o valor
    // normalizado sobrescreva o que está sendo digitado; fora da edição acompanha `source` (Descartar).
    component LiveField: TextField {
        id: lf
        property string source
        signal typed(string text)
        Component.onCompleted: text = source // sem binding: o que é digitado não é reescrito
        onTextEdited: if (acceptableInput || !validator) typed(text)
        onSourceChanged: if (!activeFocus) text = source
        onEditingFinished: text = source
    }
    Component {
        id: textControl
        LiveField { implicitWidth: 300; source: row.value; onTyped: t => row.edited(t) }
    }
    Component {
        id: hotkeyControl
        TextField {
            id: hk
            implicitWidth: 200
            readOnly: true
            horizontalAlignment: Text.AlignHCenter
            text: activeFocus ? "Pressione as teclas…" : row.value
            Keys.onPressed: event => {
                event.accepted = true
                const combo = F.combo(event)
                if (combo === null) return
                if (combo === "") { win.toastsShow("Use Ctrl, Alt ou Shift junto com a tecla.", "error"); return }
                row.edited(combo)
                hk.focus = false
            }
        }
    }
    Component {
        id: folderControl
        RowLayout {
            spacing: 8
            LiveField {
                id: folderField
                implicitWidth: 320
                source: row.value
                onTyped: t => row.edited(t)
            }
            AButton { text: "Escolher"; onClicked: folderDialog.open() }
            FolderDialog {
                id: folderDialog
                currentFolder: App.folderUrl(row.value)
                onAccepted: row.edited(App.pathFromUrl(selectedFolder))
            }
        }
    }
    Component {
        id: multilineControl
        ScrollView {
            implicitWidth: 360
            implicitHeight: 110
            TextArea {
                id: area
                Component.onCompleted: text = row.value
                placeholderText: "Meu Jogo=jogo.exe"
                wrapMode: TextEdit.NoWrap
                // Marca a alteração a cada tecla; o texto só é reposto (ex.: Descartar) fora da edição.
                onTextChanged: if (activeFocus && text !== row.value) row.edited(text)
                Connections {
                    target: row
                    function onValueChanged() { if (!area.activeFocus) area.text = row.value }
                }
            }
        }
    }

    // Jogos escolhidos: chips + "Adicionar jogo" (janela com busca e Explorer).
    Component {
        id: gamesControl
        Flow {
            id: flow
            spacing: 8
            readonly property var chosen: row.value || []
            readonly property var available: {
                const names = (row.field.options || []).map(o => o.value)
                for (const c of row.parseCustom(row.customText()))
                    if (!names.includes(c.name)) names.push(c.name)
                return names
            }
            Repeater {
                model: flow.chosen
                GameChip {
                    required property string modelData
                    game: modelData
                    onRemoved: row.edited(flow.chosen.filter(n => n !== modelData))
                }
            }
            AButton {
                text: flow.chosen.length ? "Adicionar jogo" : "Escolher jogos"
                iconName: "plus"
                implicitHeight: 36
                onClicked: picker.open()
            }
            Text {
                visible: flow.chosen.length === 0
                height: 36
                verticalAlignment: Text.AlignVCenter
                text: "Nenhum jogo escolhido."
                color: Theme.faint
                font.pixelSize: 12
            }
            GamePicker {
                id: picker
                games: flow.available
                selected: flow.chosen
                onToggled: (game, on) => row.edited(on ? flow.chosen.concat([game]) : flow.chosen.filter(n => n !== game))
                onExePicked: g => {
                    const name = row.addCustom(g)
                    if (!flow.chosen.includes(name)) row.edited(flow.chosen.concat([name]))
                }
            }
        }
    }

    // Jogos personalizados: chips com o executável + "Adicionar pelo executável" (Explorer).
    Component {
        id: customGamesControl
        Flow {
            id: customFlow
            spacing: 8
            readonly property var games: row.parseCustom(row.value)
            Repeater {
                model: customFlow.games
                GameChip {
                    required property var modelData
                    game: modelData.name
                    detail: modelData.exes.join(", ")
                    onRemoved: {
                        row.edited(customFlow.games.filter(g => g.line !== modelData.line).map(g => g.line).join("\n"))
                        const chosen = row.lookup("GameList") || []
                        if (chosen.includes(modelData.name))
                            row.editedKey("GameList", chosen.filter(n => n !== modelData.name))
                    }
                }
            }
            AButton {
                text: "Adicionar pelo executável…"
                iconName: "folder"
                implicitHeight: 36
                onClicked: exeDialog.open()
            }
            FileDialog {
                id: exeDialog
                title: "Escolha o executável do jogo"
                nameFilters: ["Executáveis (*.exe)"]
                currentFolder: "file:///C:/Program Files"
                onAccepted: {
                    const g = App.gameFromExe(selectedFile)
                    if (g && g.name) row.addCustom(g)
                }
            }
        }
    }
}
