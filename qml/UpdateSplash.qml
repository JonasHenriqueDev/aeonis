import QtQuick
import QtQuick.Window

// Tela de atualização (estilo Discord): logo pulsando, estado e barra de progresso. O contexto
// "Updater" é o AutoUpdater; a janela some sozinha quando o app fecha para o instalador rodar.
Window {
    id: splash
    width: 300
    height: 340
    visible: true
    title: "Atualizando o Aeonis"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"

    // O Updater pode sumir antes da janela quando o app fecha para o instalador rodar.
    readonly property real progress: Updater ? Updater.progress : -1
    readonly property bool installing: Updater ? Updater.busy && progress < 0 : true
    readonly property string fromVersion: Updater ? Updater.version : ""
    readonly property string toVersion: Updater ? Updater.newVersion : ""

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: Theme.side
        border.color: Theme.line

        // Arrastar pela janela inteira.
        DragHandler { onActiveChanged: if (active) splash.startSystemMove() }

        Column {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -6
            spacing: 18
            width: parent.width - 56

            Item {
                width: 120
                height: 120
                anchors.horizontalCenter: parent.horizontalCenter
                // Brilho atrás da logo.
                Rectangle {
                    anchors.centerIn: parent
                    width: 112; height: 112; radius: 56
                    color: Theme.accent
                    opacity: 0.18
                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.85; to: 1.12; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.12; to: 0.85; duration: 900; easing.type: Easing.InOutSine }
                    }
                }
                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/aeonis-256.png"
                    sourceSize: Qt.size(192, 192)
                    width: 96
                    height: 96
                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        NumberAnimation { from: 1; to: 1.06; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.06; to: 1; duration: 900; easing.type: Easing.InOutSine }
                    }
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: splash.installing ? "Instalando atualização" : "Baixando atualização"
                color: Theme.text
                font.family: Theme.font
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }

            // Barra de progresso; sem total conhecido (instalando), um trecho corre de um lado ao outro.
            Rectangle {
                id: track
                width: parent.width
                height: 6
                radius: 3
                color: Theme.cardHi
                clip: true
                Rectangle {
                    id: fill
                    height: parent.height
                    radius: 3
                    width: splash.progress >= 0 ? Math.max(height, parent.width * splash.progress) : parent.width * 0.3
                    Behavior on width { enabled: splash.progress >= 0; NumberAnimation { duration: 150 } }
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Theme.gradA }
                        GradientStop { position: 1; color: Theme.gradB }
                    }
                    NumberAnimation on x {
                        running: splash.progress < 0
                        loops: Animation.Infinite
                        from: -track.width * 0.3
                        to: track.width
                        duration: 1100
                        easing.type: Easing.InOutQuad
                    }
                    onWidthChanged: if (splash.progress >= 0) x = 0
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: splash.progress >= 0 ? Math.round(splash.progress * 100) + "%  ·  versão " + splash.toVersion
                                           : "O Aeonis vai reabrir sozinho em instantes."
                color: Theme.muted
                font.family: Theme.font
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }

        Text {
            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 14 }
            text: splash.fromVersion + "  →  " + splash.toVersion
            color: Theme.faint
            font.family: Theme.font
            font.pixelSize: 11
        }
    }
}
