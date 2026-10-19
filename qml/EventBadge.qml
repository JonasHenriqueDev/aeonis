import QtQuick
import "Format.js" as F

// Selo redondo de um evento (abate, torre, dragão, morte...): cor do tipo + ícone.
Rectangle {
    id: badge
    property string label: ""
    property string icon: F.eventIcon(label)
    property int size: 22
    width: size
    height: size
    radius: size / 2
    color: Theme.kindColor(F.labelKind(label))
    Icon {
        anchors.centerIn: parent
        name: badge.icon
        size: Math.round(badge.size * 0.62)
        color: "white"
    }
}
