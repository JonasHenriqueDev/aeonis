import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Barra lateral: marca, abas, jogos (com contagem) e o status da captura.
Rectangle {
    color: Theme.side
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

    component NavButton: AbstractButton {
        id: nb
        property string iconName
        property bool on: false
        property int count: -1
        property string game: ""
        property string gameIconUrl: ""
        Layout.fillWidth: true
        implicitHeight: 38
        hoverEnabled: true
        background: Rectangle {
            radius: 9
            color: nb.on ? Theme.cardHi : nb.hovered ? Theme.card : "transparent"
        }
        contentItem: RowLayout {
            spacing: 10
            Item { width: 2 }
            Icon { visible: nb.game === "" ; name: nb.iconName; size: 18; color: nb.on ? Theme.text : Theme.muted }
            GameAvatar { visible: nb.game !== ""; game: nb.game; iconUrl: nb.gameIconUrl; size: 24 }
            Text {
                Layout.fillWidth: true
                text: nb.text
                elide: Text.ElideRight
                color: nb.on ? Theme.text : Theme.muted
                font.pixelSize: 14
                font.weight: nb.on ? Font.DemiBold : Font.Normal
            }
            Text {
                visible: nb.count >= 0
                text: nb.count
                color: Theme.faint
                font.pixelSize: 12
            }
            Item { width: 4 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        anchors.topMargin: 18
        spacing: 4

        RowLayout {
            Layout.leftMargin: 8
            Layout.bottomMargin: 16
            spacing: 12
            Image {
                source: "qrc:/icons/aeonis-64.png"
                sourceSize: Qt.size(76, 76)
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
            }
            ColumnLayout {
                spacing: 0
                Text { text: "Aeonis"; color: Theme.text; font.pixelSize: 17; font.bold: true }
                Text { text: "gravador de gameplay"; color: Theme.faint; font.pixelSize: 12 }
            }
        }

        NavButton { text: "Partidas"; iconName: "trophy"; on: win.view === "matches" && !win.current; onClicked: win.go("matches") }
        NavButton { text: "Clipes"; iconName: "film"; on: win.view === "clips" && !win.current; onClicked: win.go("clips") }

        Text {
            text: "JOGOS"
            color: Theme.faint
            font.pixelSize: 11
            font.letterSpacing: 1
            Layout.topMargin: 18
            Layout.leftMargin: 10
            Layout.bottomMargin: 4
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            header: NavButton {
                width: ListView.view.width
                text: "Todos os jogos"
                iconName: "grid"
                on: win.gameFilter === ""
                count: (win.lib.games || []).reduce((n, g) => n + g.clips, 0)
                onClicked: { win.gameFilter = ""; if (win.view === "settings") win.view = "matches"; win.current = null }
            }
            model: win.lib.games || []
            delegate: NavButton {
                required property var modelData
                width: ListView.view.width
                text: F.gameName(modelData.name)
                game: modelData.name
                gameIconUrl: modelData.icon
                count: modelData.clips
                on: win.gameFilter === modelData.name
                onClicked: { win.gameFilter = modelData.name; if (win.view === "settings") win.view = "matches"; win.current = null }
            }
        }

        NavButton { text: "Configurações"; iconName: "gear"; on: win.view === "settings" && !win.current; onClicked: win.go("settings") }

        // Status da captura
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 8
            implicitHeight: statusRow.implicitHeight + 20
            radius: 10
            color: Theme.panel
            border.color: Theme.line
            readonly property var st: App.status
            RowLayout {
                id: statusRow
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                Rectangle {
                    width: 10; height: 10; radius: 5
                    color: parent.parent.st.state === "recording" ? Theme.kill : parent.parent.st.state === "buffer" ? Theme.assist : Theme.faint
                    SequentialAnimation on opacity {
                        running: App.status.state === "recording"
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 700 }
                        NumberAnimation { to: 1; duration: 700 }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: 13
                        text: App.status.state === "recording" ? "Gravando · " + F.gameName(App.status.game)
                            : App.status.state === "buffer" ? "Buffer ativo · " + F.gameName(App.status.game) : "Aguardando jogo"
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Theme.faint
                        font.pixelSize: 11
                        text: App.status.state === "idle" ? App.status.hotkeySave + " salva clipe · v" + App.status.version
                                                          : App.status.encoder + " · " + App.status.hotkeySave + " salva clipe"
                    }
                }
            }
        }
    }
}
