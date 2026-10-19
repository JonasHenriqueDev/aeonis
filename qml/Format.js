.pragma library

const MONTHS = ["janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"]
const WEEKDAYS = ["domingo", "segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado"]

function pad(n) { return (n < 10 ? "0" : "") + n }

function dayLabel(d) {
    const x = new Date(d); x.setHours(0, 0, 0, 0)
    const today = new Date(); today.setHours(0, 0, 0, 0)
    const diff = Math.round((today - x) / 86400000)
    if (diff === 0) return "Hoje"
    if (diff === 1) return "Ontem"
    let s = WEEKDAYS[x.getDay()] + ", " + x.getDate() + " de " + MONTHS[x.getMonth()]
    if (x.getFullYear() !== today.getFullYear()) s += " de " + x.getFullYear()
    return s.charAt(0).toUpperCase() + s.slice(1)
}

function time(d) { const x = new Date(d); return pad(x.getHours()) + ":" + pad(x.getMinutes()) }

function dur(s) {
    if (s === undefined || s === null || s < 0 || isNaN(s)) return "–:–"
    s = Math.round(s)
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60
    return h ? h + ":" + pad(m) + ":" + pad(sec) : m + ":" + pad(sec)
}

function precise(s) {
    const m = Math.floor(s / 60), sec = (s % 60).toFixed(1)
    return m + ":" + (sec < 10 ? "0" : "") + sec
}

function size(b) {
    if (b >= 1073741824) return (b / 1073741824).toFixed(2) + " GB"
    return (b / 1048576).toFixed(b >= 104857600 ? 0 : 1) + " MB"
}

function span(a, b) {
    const min = Math.max(1, Math.round((new Date(b) - new Date(a)) / 60000))
    return min >= 60 ? Math.floor(min / 60) + " h " + (min % 60) + " min" : min + " min"
}

function gameName(g) { return g === "Desktop" ? "Área de trabalho" : g }

function plural(n, one, many) { return n + " " + (n === 1 ? one : many) }

// Cor do selo de um clipe: tipo (partida/manual) ou o evento do destaque.
function labelKind(l) {
    if (/kill|ace|abates/i.test(l)) return "multi"
    if (/abate/i.test(l)) return "kill"
    if (/drag|bar[ãa]o|arauto|torre|inibidor|vastilarva|atakhan/i.test(l)) return "obj"
    if (/morte/i.test(l)) return "death"
    if (/assist/i.test(l)) return "assist"
    return "manual"
}

// Ícone de cada tipo de evento/destaque (Icon.qml), no estilo dos marcadores do Outplayed.
function eventIcon(l) {
    l = l || ""
    if (/^partida|grava[çc][ãa]o|recuperado/i.test(l)) return "play"
    if (/ace/i.test(l) && !/abate/i.test(l)) return "crown"
    if (/kill|abates/i.test(l)) return "swords"
    if (/abate/i.test(l)) return "sword"
    if (/morte/i.test(l)) return "skull"
    if (/assist/i.test(l)) return "assist"
    if (/torre/i.test(l)) return "tower"
    if (/inibidor/i.test(l)) return "inhib"
    if (/drag/i.test(l)) return "dragon"
    if (/bar[ãa]o|atakhan/i.test(l)) return "baron"
    if (/arauto/i.test(l)) return "herald"
    if (/vastilarva/i.test(l)) return "grubs"
    return "film"
}

function clipIcon(c) { return c.kind === "full" ? "play" : c.kind === "manual" ? "film" : eventIcon(c.label) }

// Resumo dos eventos de uma partida: [{ icon, label, kind, count }] na ordem de importância.
function eventSummary(events) {
    const order = ["crown", "swords", "sword", "assist", "skull", "baron", "dragon", "herald", "grubs", "inhib", "tower"]
    const map = {}
    for (const e of (events || [])) {
        const icon = eventIcon(e.label)
        if (!map[icon]) map[icon] = { icon: icon, label: e.label, kind: labelKind(e.label), count: 0 }
        map[icon].count++
    }
    return order.filter(i => map[i]).map(i => map[i])
}

function kda(info) {
    if (!info || info.kills === undefined) return ""
    return info.kills + " / " + info.deaths + " / " + info.assists
}

function kdaRatio(info) {
    if (!info || info.kills === undefined) return ""
    if (!info.deaths) return "KDA perfeito"
    return ((info.kills + info.assists) / info.deaths).toFixed(2) + " KDA"
}

function resultText(info) {
    if (!info || !info.result) return ""
    return info.result === "Win" ? "Vitória" : info.result === "Lose" ? "Derrota" : info.result
}

function modeText(info) { return info ? (info.queue || info.mode || "") : "" }

function matchMain(m) {
    if (!m || !m.clips.length) return null
    return m.clips.find(c => c.kind === "full") || m.clips[0]
}

function kindOf(c) {
    if (c.kind === "full") return "full"
    if (c.kind === "manual") return "manual"
    return labelKind(c.label)
}

function chipText(c) {
    if (c.kind === "full") return "Partida completa"
    if (c.kind === "manual" && c.label === "Clipe") return "Clipe manual"
    return c.label
}

function hue(name) {
    let h = 2166136261
    for (let i = 0; i < name.length; ++i) h = Math.imul(h ^ name.charCodeAt(i), 16777619) >>> 0
    return h % 360
}

function initials(game) {
    const words = gameName(game).split(/[\s\-:]+/).filter(w => w && !/^(of|the|de|da|do)$/i.test(w))
    return (words.length > 1 ? words.slice(0, 2).map(w => w[0]).join("") : (words[0] || "?").slice(0, 2)).toUpperCase()
}

// Combinação de teclas do QML -> texto das configurações ("Alt+F10"). null se não servir.
function combo(event) {
    const k = event.key
    let key = null
    if (k >= Qt.Key_A && k <= Qt.Key_Z) key = String.fromCharCode(k)
    else if (k >= Qt.Key_0 && k <= Qt.Key_9) key = (event.modifiers & Qt.KeypadModifier) ? "NumPad" + (k - Qt.Key_0) : String.fromCharCode(k)
    else if (k >= Qt.Key_F1 && k <= Qt.Key_F24) key = "F" + (k - Qt.Key_F1 + 1)
    else {
        const named = {}
        named[Qt.Key_Insert] = "Insert"; named[Qt.Key_Home] = "Home"; named[Qt.Key_End] = "End"
        named[Qt.Key_PageUp] = "PageUp"; named[Qt.Key_PageDown] = "PageDown"; named[Qt.Key_Pause] = "Pause"
        named[Qt.Key_Space] = "Space"; named[Qt.Key_Print] = "PrintScreen"; named[Qt.Key_Delete] = "Delete"
        key = named[k] || null
    }
    if (!key) return null
    const parts = []
    if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
    if (event.modifiers & Qt.AltModifier) parts.push("Alt")
    if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
    if (event.modifiers & Qt.MetaModifier) parts.push("Win")
    if (!parts.length && !/^F\d/.test(key)) return ""
    parts.push(key)
    return parts.join("+")
}

// Nome de cada tipo de evento (filtro do player).
function kindName(icon) {
    return ({ crown: "Ace", swords: "Multikill", sword: "Abate", skull: "Morte", assist: "Assistência",
              baron: "Barão", dragon: "Dragão", herald: "Arauto", grubs: "Vastilarvas", inhib: "Inibidor",
              tower: "Torre", film: "Outros" })[icon] || icon
}

// Chave do dia local (aaaa-mm-dd), para agrupar sessões.
function dayKey(d) { const x = new Date(d); return x.getFullYear() + "-" + pad(x.getMonth() + 1) + "-" + pad(x.getDate()) }

function two(n) { return n < 10 ? "0" + n : "" + n }
