import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Barra de título própria. Arrastar, duplo clique e Aero Snap vêm do Windows (TitleBarFilter, em
// WindowHost.cpp, trata esta área como "caption"); só os botões à direita recebem cliques aqui.
Rectangle {
    id: bar
    readonly property int buttonWidth: 46
    readonly property int buttonsWidth: buttonWidth * 3
    color: Theme.side
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }

    component CaptionButton: AbstractButton {
        id: cb
        property string iconName
        property bool danger: false
        Layout.preferredWidth: bar.buttonWidth
        Layout.fillHeight: true
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            color: cb.danger ? (cb.pressed ? "#b3101e" : cb.hovered ? "#e81123" : "transparent")
                             : (cb.pressed ? Theme.cardHi : cb.hovered ? Theme.card : "transparent")
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: cb.iconName
                size: 14
                color: cb.danger && cb.hovered ? "white" : Theme.muted
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Image {
            Layout.leftMargin: 14
            source: "qrc:/icons/aeonis-32.png"
            sourceSize: Qt.size(36, 36)
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
        }
        Text {
            Layout.leftMargin: 10
            text: "Aeonis"
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        // Estado da captura, discreto, ao lado do nome.
        Rectangle {
            Layout.leftMargin: 12
            visible: App.status.state === "recording" || App.status.state === "buffer"
            implicitWidth: pill.implicitWidth + 18
            implicitHeight: 20
            radius: 10
            color: Theme.card
            border.color: Theme.line
            RowLayout {
                id: pill
                anchors.centerIn: parent
                spacing: 6
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    color: App.status.state === "recording" ? Theme.kill : Theme.assist
                }
                Text {
                    text: (App.status.state === "recording" ? "Gravando · " : "Buffer · ") + F.gameName(App.status.game)
                    color: Theme.muted
                    font.pixelSize: 11
                }
            }
        }
        Item { Layout.fillWidth: true }

        RowLayout {
            Layout.fillHeight: true
            spacing: 0
            CaptionButton { iconName: "minimize"; onClicked: win.showMinimized() }
            CaptionButton {
                iconName: win.visibility === Window.Maximized ? "restore" : "maximize"
                onClicked: win.visibility === Window.Maximized ? win.showNormal() : win.showMaximized()
            }
            CaptionButton { iconName: "close"; danger: true; onClicked: win.close() }
        }
    }
}
