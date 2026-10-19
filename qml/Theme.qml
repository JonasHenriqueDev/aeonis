pragma Singleton
import QtQuick

// Cores da marca Aeonis (violeta -> azul -> ciano) sobre o estilo FluentWinUI3 escuro.
QtObject {
    readonly property color bg: "#0e0f13"
    readonly property color side: "#121319"
    readonly property color panel: "#171820"
    readonly property color card: "#1c1d26"
    readonly property color cardHi: "#23242f"
    readonly property color line: "#272936"
    readonly property color text: "#ecebf3"
    readonly property color muted: "#8d8fa1"
    readonly property color faint: "#5d5f70"
    readonly property color accent: "#8b3dff"
    readonly property color accent2: "#2563eb"
    readonly property color gradA: "#7c3aed"
    readonly property color gradB: "#22e5f5"
    readonly property color kill: "#f43f5e"
    readonly property color multi: "#f59e0b"
    readonly property color obj: "#a855f7"
    readonly property color death: "#64748b"
    readonly property color assist: "#22c55e"
    readonly property color manual: "#0ea5e9"
    readonly property color full: "#6366f1"
    readonly property color star: "#facc15"
    readonly property int radius: 12
    readonly property string font: "Segoe UI Variable Text"

    function kindColor(kind) {
        switch (kind) {
        case "full": return full
        case "kill": return kill
        case "multi": return multi
        case "obj": return obj
        case "death": return death
        case "assist": return assist
        default: return manual
        }
    }
}
