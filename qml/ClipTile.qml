import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as F

// Miniatura de um vídeo: selo do tipo/evento, duração, ★ salvo e hora.
AbstractButton {
    id: tile
    property var item
    property bool showGame: false
    property bool compact: false
    implicitWidth: compact ? 200 : 252
    implicitHeight: thumb.height + (compact ? 0 : 34)
    hoverEnabled: true
    onClicked: win.openClip(item)

    background: Item {}
    contentItem: ColumnLayout {
        spacing: 6
        Rectangle {
            id: thumb
            Layout.fillWidth: true
            Layout.preferredHeight: tile.width * 9 / 16
            radius: 10
            color: Theme.card
            border.color: tile.hovered ? Theme.accent : Theme.line
            clip: true
            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: win.thumbOf(tile.item)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 480
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 8
                radius: 6
                height: 22
                width: chip.implicitWidth + 14
                color: Theme.kindColor(F.kindOf(tile.item))
                Text {
                    id: chip
                    anchors.centerIn: parent
                    text: F.chipText(tile.item)
                    color: "white"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
            Icon {
                visible: tile.item.saved
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                name: "starFill"
                size: 18
                color: Theme.star
            }
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                radius: 5
                height: 20
                width: durText.implicitWidth + 12
                color: "#cc000000"
                Text {
                    id: durText
                    anchors.centerIn: parent
                    text: F.dur(win.durationOf(tile.item))
                    color: "white"
                    font.pixelSize: 11
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 46; height: 46; radius: 23
                color: "#99000000"
                visible: tile.hovered
                Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 2; name: "play"; size: 20; color: "white" }
            }
        }
        RowLayout {
            visible: !tile.compact
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: (tile.showGame ? F.gameName(tile.item.game) + " · " : "") + F.time(tile.item.created)
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text { text: F.size(tile.item.size); color: Theme.faint; font.pixelSize: 12 }
        }
    }
}
