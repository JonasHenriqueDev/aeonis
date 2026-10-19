import QtQuick

// Avisos curtos no canto inferior direito.
Column {
    id: t
    spacing: 8
    function show(text, kind) { toastModel.append({ message: text, kind: kind || "info", born: Date.now() }) }

    ListModel { id: toastModel }
    Timer {
        interval: 500
        repeat: true
        running: toastModel.count > 0
        onTriggered: {
            while (toastModel.count > 0 && Date.now() - toastModel.get(0).born > 3500)
                toastModel.remove(0)
        }
    }
    Repeater {
        model: toastModel
        delegate: Rectangle {
            id: toast
            required property string message
            required property string kind
            width: Math.min(420, label.implicitWidth + 32)
            height: label.implicitHeight + 20
            radius: 10
            color: kind === "error" ? "#3b1219" : Theme.cardHi
            border.color: kind === "error" ? Theme.kill : Theme.line
            Text {
                id: label
                anchors.centerIn: parent
                width: Math.min(388, implicitWidth)
                text: toast.message
                color: Theme.text
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }
        }
    }
}
