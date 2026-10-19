import QtQuick

// Roda do mouse em listas: um passo fixo por clique com uma animação curta, no lugar do "flick"
// padrão do Qt (que rola pouco e devagar no desktop). Use dentro de um ListView/Flickable.
WheelHandler {
    id: h
    property Flickable flick: parent
    property bool horizontal: false
    property real step: 160 // px por clique da roda
    property real target: 0
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

    property NumberAnimation anim: NumberAnimation {
        target: h.flick
        property: h.horizontal ? "contentX" : "contentY"
        to: h.target
        duration: 120
        easing.type: Easing.OutCubic
    }

    function bounds() {
        const f = flick
        return horizontal ? [f.originX - f.leftMargin, f.originX + Math.max(0, f.contentWidth + f.leftMargin + f.rightMargin - f.width) - f.leftMargin]
                          : [f.originY - f.topMargin, f.originY + Math.max(0, f.contentHeight + f.topMargin + f.bottomMargin - f.height) - f.topMargin]
    }
    function clamp(v) { const b = bounds(); return Math.max(b[0], Math.min(v, b[1])) }

    onWheel: event => {
        const now = horizontal ? flick.contentX : flick.contentY
        const delta = event.pixelDelta.y !== 0 || event.pixelDelta.x !== 0
                      ? (horizontal && event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.pixelDelta.y)
                      : (horizontal && event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y) / 120 * step
        if (event.pixelDelta.y !== 0 || event.pixelDelta.x !== 0) { // touchpad: segue o dedo
            anim.stop()
            target = clamp(now - delta)
            if (horizontal) flick.contentX = target
            else flick.contentY = target
        } else {
            target = clamp((anim.running ? target : now) - delta)
            anim.restart()
        }
        event.accepted = true
    }
}
