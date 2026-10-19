import QtQuick
import QtQuick.Controls

// Rolagem leve para páginas longas: a roda do mouse move um passo fixo com uma animação curta (sem a
// inércia do Flickable, que deixa a rolagem "pesada" no desktop). Arrastar fica só na barra.
Flickable {
    id: f
    property real step: 120 // px por clique da roda
    property real targetY: 0
    clip: true
    interactive: false
    boundsBehavior: Flickable.StopAtBounds
    pixelAligned: true
    contentWidth: width
    ScrollBar.vertical: ScrollBar { policy: f.contentHeight > f.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

    function clampY(y) { return Math.max(0, Math.min(y, Math.max(0, contentHeight - height))) }
    function scrollTo(y) {
        targetY = clampY(y)
        anim.restart()
    }
    onContentHeightChanged: if (contentY > clampY(contentY)) contentY = clampY(contentY)

    NumberAnimation {
        id: anim
        target: f
        property: "contentY"
        to: f.targetY
        duration: 140
        easing.type: Easing.OutCubic
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            const from = anim.running ? f.targetY : f.contentY
            if (event.pixelDelta.y !== 0) { // touchpad: segue o dedo, sem animação
                anim.stop()
                f.contentY = f.clampY(f.contentY - event.pixelDelta.y)
                f.targetY = f.contentY
            } else {
                f.scrollTo(from - event.angleDelta.y / 120 * f.step)
            }
            event.accepted = true
        }
    }
}
