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
    signal edited(var value)
    implicitHeight: Math.max(labels.implicitHeight, control.implicitHeight) + 20

    ColumnLayout {
        id: labels
        anchors { left: parent.left; verticalCenter: parent.verticalCenter; right: control.left; rightMargin: 24 }
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
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        sourceComponent: {
            switch (row.field.type) {
            case "bool": return boolControl
            case "select": return selectControl
            case "preset": return presetControl
            case "int": case "number": return numberControl
            case "hotkey": return hotkeyControl
            case "folder": return folderControl
            case "multiline": return multilineControl
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
}
