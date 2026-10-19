"use strict";

// ---------------------------------------------------------------- ícones (SVG inline, sem dependências)
const ICONS = {
  trophy: '<path d="M8 21h8M12 17v4M7 4h10v5a5 5 0 0 1-10 0V4Z"/><path d="M17 5h3v2a3 3 0 0 1-3 3M7 5H4v2a3 3 0 0 0 3 3"/>',
  film: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 4v16M17 4v16M3 9h4M3 15h4M17 9h4M17 15h4"/>',
  gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1Z"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
  play: '<path d="M7 4.5v15a1 1 0 0 0 1.5.9l12-7.5a1 1 0 0 0 0-1.8l-12-7.5A1 1 0 0 0 7 4.5Z" fill="currentColor" stroke="none"/>',
  back: '<path d="M15 18l-6-6 6-6"/>',
  folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7Z"/>',
  trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12M9 7V4h6v3"/>',
  scissors: '<circle cx="6" cy="6" r="3"/><circle cx="6" cy="18" r="3"/><path d="M20 4 8.1 15.9M14.5 14.5 20 20M8.1 8.1 12 12"/>',
  external: '<path d="M14 4h6v6M20 4l-9 9M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5"/>',
  refresh: '<path d="M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"/>',
  bolt: '<path d="M13 2 4 14h7l-1 8 9-12h-7l1-8Z"/>',
};
function icon(name) {
  return `<i data-icon="${name}"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">${ICONS[name]}</svg></i>`;
}
function hydrateIcons(root = document) {
  root.querySelectorAll("i[data-icon]:empty").forEach(el => (el.outerHTML = icon(el.dataset.icon)));
}

// ---------------------------------------------------------------- estado e ponte com o app
const host = window.chrome?.webview;
const S = {
  lib: { matches: [], games: [], totalBytes: 0 },
  clips: new Map(),       // id -> clip
  matchOf: new Map(),     // id do clipe -> partida
  fields: [],
  status: null,
  view: "matches",
  game: null,             // filtro de jogo (null = todos)
  dirty: {},
  current: null,          // clipe aberto no player
  trim: null,             // { a, b } em segundos
};
const send = (cmd, data = {}) => host?.postMessage({ cmd, ...data });
const $ = sel => document.querySelector(sel);

host?.addEventListener("message", e => {
  const m = e.data;
  switch (m.type) {
    case "library": setLibrary(m.data); break;
    case "settings": S.fields = m.fields; S.dirty = {}; if (S.view === "settings") render(); break;
    case "status": S.status = m.data; renderStatus(); break;
    case "navigate": go(m.page); break;
    case "toast": toast(m.text, m.kind); break;
    case "thumb": onThumb(m); break;
    case "trimDone": if (m.ok) S.trim = null, renderTrim(); break;
    case "picked": S.dirty[m.key] = m.value; render(); break;
  }
});

function setLibrary(lib) {
  S.lib = lib;
  S.clips.clear();
  S.matchOf.clear();
  for (const m of lib.matches) for (const c of m.clips) { S.clips.set(c.id, c); S.matchOf.set(c.id, m); }
  if (S.game && !lib.games.some(g => g.name === S.game)) S.game = null;
  renderGames();
  if (S.view !== "settings") render();
  if (S.current) {
    if (!S.clips.has(S.current.id)) closePlayer();
    else { S.current = S.clips.get(S.current.id); renderSide(); }
  }
}

function onThumb(m) {
  const c = S.clips.get(m.id);
  if (!c) return;
  if (m.url) c.thumb = m.url;
  if (m.duration) c.duration = m.duration;
  document.querySelectorAll(`[data-thumb="${m.id}"]`).forEach(img => { if (m.url) loadImg(img, m.url); });
  document.querySelectorAll(`[data-dur="${m.id}"]`).forEach(el => (el.textContent = fmtDur(c.duration)));
}

// ---------------------------------------------------------------- formatação
const DAY = new Intl.DateTimeFormat("pt-BR", { weekday: "long", day: "numeric", month: "long" });
const DAY_Y = new Intl.DateTimeFormat("pt-BR", { weekday: "long", day: "numeric", month: "long", year: "numeric" });
const TIME = new Intl.DateTimeFormat("pt-BR", { hour: "2-digit", minute: "2-digit" });
const esc = s => String(s ?? "").replace(/[&<>"']/g, ch => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[ch]));
const gameName = g => (g === "Desktop" ? "Área de trabalho" : g);

function dayLabel(d) {
  const today = new Date(); today.setHours(0, 0, 0, 0);
  const x = new Date(d); x.setHours(0, 0, 0, 0);
  const diff = Math.round((today - x) / 86400000);
  if (diff === 0) return "Hoje";
  if (diff === 1) return "Ontem";
  return (x.getFullYear() === today.getFullYear() ? DAY : DAY_Y).format(x);
}
function fmtDur(s) {
  if (s == null || isNaN(s)) return "–:–";
  s = Math.round(s);
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  return h ? `${h}:${String(m).padStart(2, "0")}:${String(sec).padStart(2, "0")}` : `${m}:${String(sec).padStart(2, "0")}`;
}
function fmtPrecise(s) {
  const m = Math.floor(s / 60), sec = (s % 60).toFixed(1).padStart(4, "0");
  return `${m}:${sec}`;
}
function fmtSize(b) {
  return b >= 1073741824 ? (b / 1073741824).toFixed(2) + " GB" : (b / 1048576).toFixed(b >= 104857600 ? 0 : 1) + " MB";
}
function fmtSpan(a, b) {
  const min = Math.max(1, Math.round((new Date(b) - new Date(a)) / 60000));
  return min >= 60 ? `${Math.floor(min / 60)} h ${min % 60} min` : `${min} min`;
}

function kindClass(c) {
  if (c.kind === "full") return "k-full";
  if (c.kind === "manual") return "k-manual";
  return labelClass(c.label);
}
function labelClass(l) {
  if (/kill|ace|abates/i.test(l)) return "k-multi";
  if (/abate/i.test(l)) return "k-kill";
  if (/drag|bar[ãa]o|arauto|torre|inibidor/i.test(l)) return "k-obj";
  if (/morte/i.test(l)) return "k-death";
  if (/assist/i.test(l)) return "k-assist";
  return "k-manual";
}
function chipText(c) {
  if (c.kind === "full") return "Partida completa";
  if (c.kind === "manual" && c.label === "Clipe") return "Clipe manual";
  return c.label;
}

function avatar(game, cls = "") {
  const g = S.lib.games.find(x => x.name === game);
  if (g?.icon) return `<span class="gicon ${cls}"><img src="${esc(g.icon)}" alt=""></span>`;
  let h = 2166136261; // FNV-1a: espalha bem nomes parecidos
  for (const ch of game) h = Math.imul(h ^ ch.charCodeAt(0), 16777619) >>> 0;
  const hue = h % 360;
  const words = gameName(game).split(/[\s\-:]+/).filter(w => w && !/^(of|the|de|da|do)$/i.test(w));
  const initials = (words.length > 1 ? words.slice(0, 2).map(w => w[0]).join("") : (words[0] || "?").slice(0, 2)).toUpperCase();
  return `<span class="gicon ${cls}" style="background:linear-gradient(135deg,hsl(${hue} 60% 45%),hsl(${(hue + 40) % 360} 65% 35%))">${esc(initials)}</span>`;
}

function loadImg(img, url) {
  img.onload = () => img.classList.add("ok");
  img.src = url;
}

// ---------------------------------------------------------------- navegação
function go(view) {
  if (!view) return;
  if (view === "library") view = "matches";
  closePlayer();
  S.view = view;
  render();
}

document.addEventListener("click", e => {
  const t = e.target.closest("[data-view],[data-game],[data-open],[data-act]");
  if (!t) return;
  if (t.dataset.view) { if (t.dataset.view !== "settings") S.game = S.game; go(t.dataset.view); }
  else if (t.dataset.game !== undefined) { S.game = t.dataset.game || null; if (S.view === "settings") S.view = "matches"; closePlayer(); render(); renderGames(); }
  else if (t.dataset.open) openPlayer(t.dataset.open);
  else if (t.dataset.act) act(t.dataset.act, t);
});

function act(name, el) {
  const c = S.current;
  switch (name) {
    case "refresh": send("refresh"); break;
    case "openFolder": send("openFolder"); break;
    case "close": closePlayer(); break;
    case "reveal": c && send("reveal", { path: c.path }); break;
    case "external": c && send("openExternal", { path: c.path }); break;
    case "delete": c && confirmDelete(c); break;
    case "trim": startTrim(); break;
    case "markA": setMark("a"); break;
    case "markB": setMark("b"); break;
    case "trimCancel": S.trim = null; renderTrim(); break;
    case "trimSave": saveTrim(); break;
    case "save": send("saveSettings", { values: S.dirty }); break;
    case "discard": S.dirty = {}; render(); break;
    case "pick": send("pick", { key: el.dataset.key, kind: el.dataset.kind }); break;
  }
}

// ---------------------------------------------------------------- render
function render() {
  document.querySelectorAll("#tabs [data-view]").forEach(b => b.classList.toggle("on", b.dataset.view === S.view));
  document.querySelector(".side-link").classList.toggle("on", S.view === "settings");
  const main = $("#main");
  main.innerHTML = S.view === "settings" ? renderSettings() : S.view === "clips" ? renderClips() : renderMatches();
  hydrateIcons(main);
  main.querySelectorAll("img[data-thumb]").forEach(img => {
    const c = S.clips.get(img.dataset.thumb);
    if (c?.thumb) loadImg(img, c.thumb);
  });
  if (S.view === "settings") wireSettings();
}

function renderGames() {
  const total = S.lib.games.reduce((n, g) => n + g.clips, 0);
  $("#games").innerHTML =
    `<button data-game="" class="${S.game ? "" : "on"}"><span class="gicon" style="background:var(--grad)">${icon("grid")}</span><span class="name">Todos os jogos</span><span class="count">${total}</span></button>` +
    S.lib.games.map(g =>
      `<button data-game="${esc(g.name)}" class="${S.game === g.name ? "on" : ""}">${avatar(g.name)}<span class="name">${esc(gameName(g.name))}</span><span class="count">${g.clips}</span></button>`
    ).join("");
}

function header(title, sub, extra = "") {
  const ic = S.game ? avatar(S.game, "lg") : "";
  return `<div class="head"><h1>${ic}<span>${esc(title)}<div class="sub">${sub}</div></span></h1><div class="grow"></div>${extra}
    <button class="btn" data-act="refresh">${icon("refresh")}Atualizar</button>
    <button class="btn" data-act="openFolder">${icon("folder")}Abrir pasta</button></div>`;
}

function matchesFiltered() {
  return S.lib.matches.filter(m => !S.game || m.game === S.game);
}

function emptyState() {
  const hk = S.status?.hotkeySave ?? "Alt+F10";
  return `<div class="empty"><div class="big">${icon("bolt")}</div>
    <h2>Nenhum clipe ainda</h2>
    <p>Abra um jogo: o Aeonis começa a gravar em segundo plano sozinho.<br>
    Pressione <kbd>${esc(hk)}</kbd> para salvar os últimos segundos, ou jogue LoL/CS2 para receber destaques automáticos.</p></div>`;
}

function renderMatches() {
  const ms = matchesFiltered();
  const title = S.game ? gameName(S.game) : "Partidas";
  const clipsN = ms.reduce((n, m) => n + m.clips.length, 0);
  const sub = ms.length ? `${ms.length} partida${ms.length > 1 ? "s" : ""} · ${clipsN} vídeo${clipsN > 1 ? "s" : ""} · ${fmtSize(S.lib.totalBytes)} no disco` : "";
  if (!ms.length) return header(title, sub) + emptyState();

  let html = "", lastDay = null;
  for (const m of ms) {
    const day = dayLabel(m.start);
    if (day !== lastDay) {
      const count = ms.filter(x => dayLabel(x.start) === day).length;
      html += `<div class="day"><h2>${esc(day)}</h2><span>${count} partida${count > 1 ? "s" : ""}</span></div>`;
      lastDay = day;
    }
    const hl = m.clips.filter(c => c.kind === "highlight").length;
    const full = m.clips.some(c => c.kind === "full");
    html += `<article class="match"><header>${avatar(m.game, "lg")}
      <div><div class="m-title">${esc(gameName(m.game))}</div>
      <div class="m-sub">${TIME.format(new Date(m.start))} – ${TIME.format(new Date(m.end))} · ${fmtSpan(m.start, m.end)}</div></div>
      <div class="m-badges">${hl ? `<span class="badge hl">${hl} destaque${hl > 1 ? "s" : ""}</span>` : ""}
      ${full ? `<span class="badge fl">Partida completa</span>` : ""}
      <span class="badge">${m.clips.length} vídeo${m.clips.length > 1 ? "s" : ""}</span></div></header>
      <div class="strip">${m.clips.map(c => tile(c)).join("")}</div></article>`;
  }
  return header(title, sub) + `<div class="content">${html}</div>`;
}

function renderClips() {
  const clips = matchesFiltered().flatMap(m => m.clips).sort((a, b) => new Date(b.created) - new Date(a.created));
  const title = S.game ? `Clipes · ${gameName(S.game)}` : "Clipes";
  const sub = clips.length ? `${clips.length} vídeo${clips.length > 1 ? "s" : ""}` : "";
  if (!clips.length) return header(title, sub) + emptyState();
  let html = "", lastDay = null, group = [];
  const flush = () => { if (group.length) html += `<div class="grid">${group.map(c => tile(c, true)).join("")}</div>`; group = []; };
  for (const c of clips) {
    const day = dayLabel(c.created);
    if (day !== lastDay) { flush(); html += `<div class="day"><h2>${esc(day)}</h2></div>`; lastDay = day; }
    group.push(c);
  }
  flush();
  return header(title, sub) + `<div class="content">${html}</div>`;
}

function tile(c, showGame = false) {
  const when = TIME.format(new Date(c.created));
  const meta = showGame ? `<b>${esc(gameName(c.game))} · ${when}</b>` : `<b>${when}</b>`;
  return `<button class="tile" data-open="${c.id}" title="${esc(c.name)}">
    <div class="thumb"><img data-thumb="${c.id}" alt=""><div class="shade"></div>
      <span class="chip ${kindClass(c)}">${esc(chipText(c))}</span>
      <span class="dur" data-dur="${c.id}">${fmtDur(c.duration)}</span>
      <div class="play"><span>${icon("play")}</span></div></div>
    <div class="t-meta">${meta}<span>${fmtSize(c.size)}</span></div></button>`;
}

function renderStatus() {
  const st = S.status;
  if (!st) return;
  const el = $("#status");
  el.className = "status " + st.state;
  const line = st.state === "recording" ? `Gravando · ${esc(gameName(st.game))}`
    : st.state === "buffer" ? `Buffer ativo · ${esc(gameName(st.game))}` : "Aguardando jogo";
  const sub = st.state === "idle" ? `${esc(st.hotkeySave)} salva clipe · v${esc(st.version)}` : `${esc(st.encoder)} · ${esc(st.hotkeySave)} salva clipe`;
  el.innerHTML = `<span class="dot"></span><div><div class="s1">${line}</div><div class="s2">${sub}</div></div>`;
}

// ---------------------------------------------------------------- player
function openPlayer(id) {
  const c = S.clips.get(id);
  if (!c) return;
  S.current = c;
  S.trim = null;
  const p = $("#player");
  p.classList.remove("hidden");
  p.innerHTML = `
    <div class="pl-top">
      <button class="btn" data-act="close">${icon("back")}Voltar</button>
      ${avatar(c.game, "lg")}
      <div><div class="pl-title">${esc(chipText(c))} · ${esc(gameName(c.game))}</div>
      <div class="pl-sub">${esc(dayLabel(c.created))}, ${TIME.format(new Date(c.created))} · ${fmtSize(c.size)}</div></div>
      <div class="pl-actions">
        <button class="btn" data-act="trim">${icon("scissors")}Cortar</button>
        <button class="btn" data-act="reveal">${icon("folder")}Mostrar na pasta</button>
        <button class="btn" data-act="external">${icon("external")}Abrir fora</button>
        <button class="btn danger" data-act="delete">${icon("trash")}Excluir</button>
      </div>
    </div>
    <div class="pl-body">
      <div class="pl-stage">
        <div class="video-wrap"><video id="vid" controls autoplay preload="auto"></video></div>
        <div class="timeline" id="tl"><div class="tl-track"></div><div class="tl-fill" id="tlFill"></div></div>
        <div id="trimBox"></div>
      </div>
      <aside class="pl-side" id="plSide"></aside>
    </div>`;
  hydrateIcons(p);
  const v = $("#vid");
  v.src = c.url;
  v.addEventListener("timeupdate", updateTimeline);
  v.addEventListener("loadedmetadata", () => { renderMarkers(); updateTimeline(); });
  v.addEventListener("error", () => {
    $(".video-wrap").insertAdjacentHTML("beforeend",
      `<div class="vid-error"><div>Não foi possível reproduzir este vídeo aqui (codec não suportado).<br><br>
       <button class="btn" data-act="external">${icon("external")}Abrir no player do Windows</button></div></div>`);
    hydrateIcons($(".video-wrap"));
  });
  $("#tl").addEventListener("click", e => {
    const r = e.currentTarget.getBoundingClientRect();
    if (v.duration) v.currentTime = ((e.clientX - r.left) / r.width) * v.duration;
  });
  renderSide();
  renderTrim();
}

function closePlayer() {
  const v = $("#vid");
  if (v) { v.pause(); v.removeAttribute("src"); v.load(); }
  S.current = null;
  S.trim = null;
  const p = $("#player");
  p.classList.add("hidden");
  p.innerHTML = "";
}

function renderSide() {
  const c = S.current, m = S.matchOf.get(c.id);
  const side = $("#plSide");
  if (!side || !m) return;
  side.innerHTML = `<h4>Desta partida · ${TIME.format(new Date(m.start))}</h4>` + m.clips.map(x => `
    <button class="mini ${x.id === c.id ? "on" : ""}" data-open="${x.id}">
      <div class="thumb"><img data-thumb="${x.id}" alt=""><span class="chip ${kindClass(x)}">${esc(chipText(x))}</span>
      <span class="dur" data-dur="${x.id}">${fmtDur(x.duration)}</span></div>
      <div><div class="mt">${esc(chipText(x))}</div><div class="ms">${TIME.format(new Date(x.created))} · ${fmtSize(x.size)}</div></div>
    </button>`).join("");
  side.querySelectorAll("img[data-thumb]").forEach(img => {
    const x = S.clips.get(img.dataset.thumb);
    if (x?.thumb) loadImg(img, x.thumb);
  });
}

function renderMarkers() {
  const v = $("#vid"), tl = $("#tl"), c = S.current;
  if (!v || !tl || !v.duration) return;
  tl.querySelectorAll(".tl-mark").forEach(x => x.remove());
  for (const mk of c.markers || []) {
    const el = document.createElement("div");
    el.className = "tl-mark " + labelClass(mk.label);
    el.style.left = (mk.t / v.duration) * 100 + "%";
    el.dataset.label = `${mk.label} · ${fmtDur(mk.t)}`;
    el.addEventListener("click", e => { e.stopPropagation(); v.currentTime = Math.max(0, mk.t - 3); v.play(); });
    tl.appendChild(el);
  }
}

function updateTimeline() {
  const v = $("#vid");
  if (!v || !v.duration) return;
  $("#tlFill").style.width = (v.currentTime / v.duration) * 100 + "%";
}

// ---------------------------------------------------------------- corte
function startTrim() {
  const v = $("#vid");
  if (!v || !v.duration) return;
  S.trim = { a: 0, b: v.duration };
  renderTrim();
}
function setMark(which) {
  const v = $("#vid");
  if (!S.trim || !v) return;
  S.trim[which] = v.currentTime;
  if (S.trim.a > S.trim.b) [S.trim.a, S.trim.b] = [S.trim.b, S.trim.a];
  renderTrim();
}
function renderTrim() {
  const box = $("#trimBox"), tl = $("#tl"), v = $("#vid");
  if (!box) return;
  tl?.querySelector(".tl-range")?.remove();
  if (!S.trim) { box.innerHTML = ""; return; }
  const { a, b } = S.trim;
  if (v?.duration) {
    const r = document.createElement("div");
    r.className = "tl-range";
    r.style.left = (a / v.duration) * 100 + "%";
    r.style.width = ((b - a) / v.duration) * 100 + "%";
    tl.prepend(r);
  }
  box.innerHTML = `<div class="trim">${icon("scissors")}
    Início <span class="tv">${fmtPrecise(a)}</span><button class="btn" data-act="markA">Marcar aqui</button>
    Fim <span class="tv">${fmtPrecise(b)}</span><button class="btn" data-act="markB">Marcar aqui</button>
    <span class="sp"></span><span class="pl-sub">${fmtDur(b - a)} · sem re-encode</span>
    <button class="btn" data-act="trimCancel">Cancelar</button>
    <button class="btn primary" data-act="trimSave">Salvar corte</button></div>`;
  hydrateIcons(box);
}
function saveTrim() {
  if (!S.trim || !S.current) return;
  send("trim", { path: S.current.path, start: S.trim.a, end: S.trim.b });
  toast("Cortando…");
}

// ---------------------------------------------------------------- exclusão
function confirmDelete(c) {
  const modal = $("#modal");
  modal.classList.remove("hidden");
  modal.innerHTML = `<div class="dialog"><h3>Excluir vídeo?</h3>
    <p>"${esc(chipText(c))}" de ${esc(gameName(c.game))} será movido para a lixeira.</p>
    <div class="acts"><button class="btn" id="mNo">Cancelar</button><button class="btn primary" id="mYes">Mover para a lixeira</button></div></div>`;
  const close = () => { modal.classList.add("hidden"); modal.innerHTML = ""; };
  $("#mNo").onclick = close;
  $("#mYes").onclick = () => {
    close();
    const path = c.path;
    const m = S.matchOf.get(c.id);
    const next = m?.clips.find(x => x.id !== c.id);
    closePlayer(); // libera o arquivo antes de excluir
    setTimeout(() => send("delete", { paths: [path] }), 150);
    if (next && S.view !== "settings") setTimeout(() => S.clips.has(next.id) && openPlayer(next.id), 900);
  };
}

// ---------------------------------------------------------------- configurações
function renderSettings() {
  const cats = [];
  for (const f of S.fields) {
    let cat = cats.find(c => c.name === f.category);
    if (!cat) cats.push(cat = { name: f.category, fields: [] });
    cat.fields.push(f);
  }
  const body = cats.map(cat => `<section class="card"><h3>${esc(cat.name)}</h3>${cat.fields.map(fieldRow).join("")}</section>`).join("");
  const n = Object.keys(S.dirty).length;
  const bar = n ? `<div class="savebar"><span>${n} alteração${n > 1 ? "ões" : ""} não salva${n > 1 ? "s" : ""}</span>
    <button class="btn" data-act="discard">Descartar</button><button class="btn primary" data-act="save">Salvar alterações</button></div>` : "";
  return `<div class="head"><h1><span>Configurações<div class="sub">Gravação, clipes, áudio e atalhos</div></span></h1></div>
    <div class="content"><div class="settings">${body}</div>${bar}</div>`;
}

function valueOf(f) { return f.key in S.dirty ? S.dirty[f.key] : f.value; }

function fieldRow(f) {
  const v = valueOf(f);
  let ctl;
  switch (f.type) {
    case "bool":
      ctl = `<label class="switch"><input type="checkbox" data-key="${f.key}" ${v ? "checked" : ""}><span></span></label>`; break;
    case "select":
      ctl = `<select data-key="${f.key}">${f.options.map(o => `<option value="${esc(o.value)}" ${o.value === v ? "selected" : ""}>${esc(o.label)}</option>`).join("")}</select>`; break;
    case "int": case "number":
      ctl = `<input type="number" data-key="${f.key}" value="${esc(v)}" step="${f.type === "int" ? 1 : 0.5}">`; break;
    case "hotkey":
      ctl = `<input type="text" class="hotkey" readonly data-key="${f.key}" value="${esc(v)}" title="Clique e pressione a combinação">`; break;
    case "folder": case "file":
      ctl = `<input type="text" data-key="${f.key}" value="${esc(v)}" placeholder="${f.type === "file" ? "Automático" : ""}">
        <button class="btn" data-act="pick" data-key="${f.key}" data-kind="${f.type}">Escolher</button>`; break;
    case "multiline":
      ctl = `<textarea data-key="${f.key}" placeholder="Meu Jogo=jogo.exe">${esc(v)}</textarea>`; break;
    default:
      ctl = `<input type="text" data-key="${f.key}" value="${esc(v)}">`;
  }
  return `<div class="row ${f.type === "multiline" ? "tall" : ""}"><div class="rl"><div class="r1">${esc(f.label)}</div>
    ${f.description ? `<div class="r2">${esc(f.description)}</div>` : ""}</div><div class="ctl">${ctl}</div></div>`;
}

function wireSettings() {
  const main = $("#main");
  const mark = (key, value) => {
    const f = S.fields.find(x => x.key === key);
    if (f && value === f.value) delete S.dirty[key]; else S.dirty[key] = value;
    const scroll = main.scrollTop;
    render();
    main.scrollTop = scroll;
  };
  main.querySelectorAll("[data-key]").forEach(el => {
    if (el.tagName === "BUTTON") return;
    const key = el.dataset.key;
    const f = S.fields.find(x => x.key === key);
    if (el.classList.contains("hotkey")) {
      el.addEventListener("focus", () => { el.classList.add("listening"); el.value = "Pressione as teclas…"; });
      el.addEventListener("blur", () => { el.classList.remove("listening"); el.value = valueOf(f); });
      el.addEventListener("keydown", e => {
        e.preventDefault();
        const combo = comboFrom(e);
        if (combo) { el.blur(); mark(key, combo); }
      });
      return;
    }
    el.addEventListener("change", () => {
      const value = f.type === "bool" ? el.checked
        : f.type === "int" ? parseInt(el.value || "0", 10)
        : f.type === "number" ? parseFloat(el.value || "0")
        : el.value;
      mark(key, value);
    });
  });
}

function comboFrom(e) {
  const code = e.code;
  if (/^(Control|Shift|Alt|Meta)(Left|Right)$/.test(code)) return null;
  let key = null, m;
  if ((m = /^Key([A-Z])$/.exec(code))) key = m[1];
  else if ((m = /^Digit(\d)$/.exec(code))) key = m[1];
  else if (/^F\d{1,2}$/.test(code)) key = code;
  else if ((m = /^Numpad(\d)$/.exec(code))) key = "NumPad" + m[1];
  else if (["Insert", "Home", "End", "PageUp", "PageDown", "Pause", "Space", "PrintScreen"].includes(code)) key = code === "PrintScreen" ? "PrintScreen" : code;
  if (!key) return null;
  const parts = [];
  if (e.ctrlKey) parts.push("Ctrl");
  if (e.altKey) parts.push("Alt");
  if (e.shiftKey) parts.push("Shift");
  if (e.metaKey) parts.push("Win");
  if (!parts.length && !/^F\d/.test(key)) { toast("Use Ctrl, Alt ou Shift junto com a tecla.", "error"); return null; }
  parts.push(key);
  return parts.join("+");
}

// ---------------------------------------------------------------- toasts e teclado
function toast(text, kind = "info") {
  const el = document.createElement("div");
  el.className = "toast " + kind;
  el.textContent = text;
  $("#toasts").appendChild(el);
  setTimeout(() => el.remove(), 3500);
}

document.addEventListener("keydown", e => {
  if (!S.current || e.target.matches("input,textarea,select")) return;
  const v = $("#vid");
  if (e.key === "Escape") { if (S.trim) { S.trim = null; renderTrim(); } else closePlayer(); }
  else if (e.key === "Delete") confirmDelete(S.current);
  else if (v && e.key === "ArrowRight") { v.currentTime = Math.min(v.duration || 0, v.currentTime + 5); e.preventDefault(); }
  else if (v && e.key === "ArrowLeft") { v.currentTime = Math.max(0, v.currentTime - 5); e.preventDefault(); }
  else if (v && e.key === " " && e.target !== v) { v.paused ? v.play() : v.pause(); e.preventDefault(); }
});

hydrateIcons();
render();
renderGames();
send("ready");
