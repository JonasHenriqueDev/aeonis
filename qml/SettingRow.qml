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
    Component {
        id: numberControl
        TextField {
            implicitWidth: 140
            text: String(row.value)
            horizontalAlignment: Text.AlignRight
            validator: row.field.type === "int" ? intValidator : doubleValidator
            onEditingFinished: {
                const v = row.field.type === "int" ? parseInt(text || "0") : parseFloat((text || "0").replace(",", "."))
                if (!isNaN(v) && v !== row.value) row.edited(v)
            }
            IntValidator { id: intValidator; bottom: 0; top: 100000 }
            DoubleValidator { id: doubleValidator; bottom: 0; top: 100000; notation: DoubleValidator.StandardNotation }
        }
    }
    Component {
        id: textControl
        TextField { implicitWidth: 300; text: row.value; onEditingFinished: if (text !== row.value) row.edited(text) }
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
            TextField {
                id: folderField
                implicitWidth: 320
                text: row.value
                onEditingFinished: if (text !== row.value) row.edited(text)
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
                text: row.value
                placeholderText: "Meu Jogo=jogo.exe"
                wrapMode: TextEdit.NoWrap
                onEditingFinished: if (text !== row.value) row.edited(text)
            }
        }
    }
}
