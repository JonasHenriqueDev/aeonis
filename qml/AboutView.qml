import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Sobre: versão, atualizações, links do projeto e licença.
Item {
    id: page
    readonly property string repo: "https://github.com/JonasHenriqueDev/aeonis"
    readonly property bool autoUpdate: {
        const f = (App.settingsFields || []).find(x => x.key === "AutoUpdate")
        return f ? f.value === true : false
    }

    component Card: Rectangle {
        default property alias content: inner.data
        Layout.fillWidth: true
        implicitHeight: inner.implicitHeight + 36
        radius: Theme.radius
        color: Theme.panel
        border.color: Theme.line
        ColumnLayout {
            id: inner
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
            spacing: 10
        }
    }

    PageHeader {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28 }
        title: "Sobre"
        subtitle: "Versão, atualizações e projeto"
        actions: false
    }

    SmoothScroll {
        id: scroll
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 16 }
        contentHeight: column.implicitHeight + 28

        ColumnLayout {
            id: column
            width: Math.min(scroll.width - 56, 980)
            x: 28
            spacing: 16

            Card {
                RowLayout {
                    spacing: 18
                    Image {
                        source: "qrc:/icons/aeonis-256.png"
                        sourceSize: Qt.size(176, 176)
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 88
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "Aeonis"; color: Theme.text; font.pixelSize: 26; font.weight: Font.Bold }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: "Gravador de gameplay leve, open-source e sem anúncios. Sem conta, sem nuvem, sem telemetria."
                            color: Theme.muted
                            font.pixelSize: 14
                        }
                        Rectangle {
                            Layout.topMargin: 6
                            implicitWidth: versionText.implicitWidth + 20
                            implicitHeight: 26
                            radius: 13
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.gradA }
                                GradientStop { position: 1; color: Theme.accent2 }
                            }
                            Text {
                                id: versionText
                                anchors.centerIn: parent
                                text: "Versão " + App.updater.version
                                color: "white"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }

            Card {
                Text { text: "Atualizações"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font.pixelSize: 13
                    text: App.updater.status
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.faint
                    font.pixelSize: 12
                    text: page.autoUpdate
                          ? "Atualização automática ligada: o Aeonis se atualiza sozinho ao abrir (nunca durante um jogo)."
                          : "Atualização automática desligada. Você pode ligar em Configurações → Geral."
                }
                RowLayout {
                    spacing: 8
                    AButton { text: "Verificar agora"; iconName: "refresh"; enabled: !App.updater.busy; onClicked: App.updater.check() }
                    AButton {
                        visible: App.updater.available
                        text: "Baixar e instalar " + App.updater.newVersion
                        iconName: "download"
                        primary: true
                        enabled: !App.updater.busy
                        onClicked: App.updater.downloadAndInstall(true)
                    }
                    AButton { text: "Novidades"; iconName: "external"; onClicked: Qt.openUrlExternally(page.repo + "/releases") }
                }
            }

            Card {
                Text { text: "Projeto"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font.pixelSize: 13
                    text: "Software livre sob a licença GPL-3.0-or-later: use, estude, modifique e redistribua à vontade. "
                          + "Feito com Qt 6 e FFmpeg."
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    AButton { text: "Código no GitHub"; iconName: "external"; onClicked: Qt.openUrlExternally(page.repo) }
                    AButton { text: "Reportar um problema"; iconName: "external"; onClicked: Qt.openUrlExternally(page.repo + "/issues") }
                    AButton { text: "Licença"; iconName: "external"; onClicked: Qt.openUrlExternally(page.repo + "/blob/main/LICENSE") }
                    AButton { text: "Abrir log"; iconName: "folder"; onClicked: App.openLog() }
                }
            }
        }
    }
}
