import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Compartilhar no YouTube: conecta a conta (uma vez), escolhe título, descrição, tags e privacidade
// e acompanha o envio, que continua com este diálogo ou a janela fechados.
Popup {
    id: d
    property var media: null
    readonly property var yt: App.youtube
    readonly property bool mine: yt.uploading && media !== null && norm(yt.uploadPath) === norm(media.path)
    readonly property string url: { yt.uploads; return media ? yt.uploadedUrl(media.path) : "" }

    function norm(p) { return (p || "").replace(/\//g, "\\").toLowerCase() }
    function openFor(m) {
        media = m
        titleField.text = (F.chipText(m) + " · " + F.gameName(m.game)).slice(0, 100)
        descField.text = ""
        tagsField.text = m.game === "Desktop" ? "" : m.game
        privacyBox.currentIndex = 0
        kidsBox.checked = false
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560
    modal: true
    padding: 22
    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }
    Overlay.modal: Rectangle { color: "#99000000" }

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            spacing: 10
            Icon { name: "youtube"; size: 22; color: "#ff0033" }
            Text { text: "Compartilhar no YouTube"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold; Layout.fillWidth: true }
            AButton { flat: true; iconName: "close"; onClicked: d.close() }
        }

        // Sem cliente OAuth nesta compilação
        Text {
            visible: !d.yt.configured
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: 13
            text: "O envio para o YouTube não está disponível nesta versão do Aeonis."
        }

        // Conectar a conta
        ColumnLayout {
            visible: d.yt.configured && !d.yt.connected
            spacing: 12
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                font.pixelSize: 13
                text: d.yt.connecting ? d.yt.status
                    : "Conecte sua conta do YouTube uma vez. O login abre no navegador, e o Aeonis só pede permissão para enviar vídeos."
            }
            RowLayout {
                spacing: 8
                AButton {
                    visible: !d.yt.connecting
                    primary: true
                    iconName: "youtube"
                    text: "Conectar conta do YouTube"
                    onClicked: d.yt.connectAccount()
                }
                BusyIndicator { visible: d.yt.connecting; running: visible; implicitWidth: 28; implicitHeight: 28 }
                AButton { visible: d.yt.connecting; text: "Cancelar"; onClicked: d.yt.cancelConnect() }
            }
        }

        // Já enviado
        Rectangle {
            visible: d.yt.connected && d.url !== "" && !d.mine
            Layout.fillWidth: true
            implicitHeight: sentRow.implicitHeight + 20
            radius: 10
            color: Theme.card
            border.color: Theme.line
            RowLayout {
                id: sentRow
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                Icon { name: "youtube"; size: 18; color: "#ff0033" }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: 13
                    text: "No YouTube: " + d.url
                }
                AButton { text: "Copiar link"; onClicked: App.copyText(d.url) }
                AButton { iconName: "external"; onClicked: App.openYouTube(d.url) }
            }
        }

        // Envio em andamento
        ColumnLayout {
            visible: d.mine
            spacing: 8
            RowLayout {
                Text { text: d.yt.status || "Enviando…"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                Text { text: Math.floor(d.yt.progress * 100) + "%"; color: Theme.muted; font.pixelSize: 13 }
            }
            ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: d.yt.progress }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                font.pixelSize: 12
                text: "Pode fechar esta janela: o envio continua em segundo plano."
            }
        }

        Text {
            visible: d.yt.connected && d.yt.uploading && !d.mine
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: 13
            text: "Outro vídeo está sendo enviado (" + Math.floor(d.yt.progress * 100) + "%). Espere terminar para enviar este."
        }

        // Formulário
        GridLayout {
            visible: d.yt.connected && !d.yt.uploading
            columns: 2
            columnSpacing: 14
            rowSpacing: 10
            Layout.fillWidth: true

            Text { text: "Título"; color: Theme.muted; font.pixelSize: 13 }
            TextField {
                id: titleField
                Layout.fillWidth: true
                maximumLength: 100
                placeholderText: "Título do vídeo"
            }

            Text { text: "Descrição"; color: Theme.muted; font.pixelSize: 13; Layout.alignment: Qt.AlignTop; Layout.topMargin: 8 }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                TextArea {
                    id: descField
                    wrapMode: TextEdit.Wrap
                    placeholderText: "Opcional"
                }
            }

            Text { text: "Tags"; color: Theme.muted; font.pixelSize: 13 }
            TextField {
                id: tagsField
                Layout.fillWidth: true
                placeholderText: "Separadas por vírgula (opcional)"
            }

            Text { text: "Visibilidade"; color: Theme.muted; font.pixelSize: 13 }
            ComboBox {
                id: privacyBox
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                model: [
                    { value: "unlisted", label: "Não listado: só quem tem o link vê" },
                    { value: "public", label: "Público" },
                    { value: "private", label: "Privado: só você vê" }
                ]
            }

            Item { implicitWidth: 1 }
            CheckBox {
                id: kidsBox
                Layout.fillWidth: true
                text: "Conteúdo feito para crianças"
                ToolTip.visible: hovered
                ToolTip.text: "O YouTube exige essa resposta (lei COPPA). Vídeos de jogo normalmente não são para crianças."
            }
        }

        RowLayout {
            spacing: 8
            Layout.topMargin: 4
            AButton {
                visible: d.yt.connected
                flat: true
                text: "Desconectar conta"
                onClicked: d.yt.disconnectAccount()
            }
            Item { Layout.fillWidth: true }
            AButton {
                visible: d.mine
                danger: true
                text: "Cancelar envio"
                onClicked: d.yt.cancelUpload()
            }
            AButton { text: d.yt.uploading || !d.yt.connected ? "Fechar" : "Cancelar"; onClicked: d.close() }
            AButton {
                visible: d.yt.connected && !d.yt.uploading
                primary: true
                iconName: "youtube"
                text: d.url !== "" ? "Enviar de novo" : "Enviar"
                enabled: titleField.text.trim() !== ""
                onClicked: App.uploadToYouTube(d.media.path, {
                    title: titleField.text,
                    description: descField.text,
                    tags: tagsField.text,
                    privacy: privacyBox.currentValue,
                    madeForKids: kidsBox.checked
                })
            }
        }
    }
}
