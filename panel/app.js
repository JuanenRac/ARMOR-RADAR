/* ARMOR-RADAR node panel - the pages. Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
   text.js (the seven languages) is joined in front of this file when the panel is packed, so LANGS and L exist here.
   No framework and no inline style: the panel is served with a strict content-security policy. */
"use strict";

// ---- small tools ---------------------------------------------------------------------------------------------------------------------

const $app = document.getElementById("app");
let lang = 0;
const t = (key, ...args) => {
  const row = L[key];
  let text = row ? (row[lang] || row[0]) : key;
  args.forEach((a, i) => { text = text.split("{" + i + "}").join(a); });
  return text;
};

function el(tag, attrs, ...kids) {
  const node = document.createElement(tag);
  for (const [name, value] of Object.entries(attrs || {})) {
    if (value === undefined || value === null || value === false) continue;
    if (name === "tip") { const text = L["tip_" + value] ? t("tip_" + value) : ""; if (text) node.title = text; continue; }   // the hover hint, in the panel's language
    if (name === "class") node.className = value;
    else if (name.startsWith("on")) node.addEventListener(name.slice(2), value);
    else if (name in node && name !== "list") node[name] = value;
    else node.setAttribute(name, value === true ? "" : value);
  }
  const add = kid => {
    if (Array.isArray(kid)) kid.forEach(add);
    else if (kid !== null && kid !== undefined && kid !== false) node.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  };
  kids.forEach(add);
  return node;
}

const S = {
  session: null, cfg: null, saved: "", channelAuto: 1, firmware: "", status: null, catalog: [], live: [], radars: [], users: [],
  page: "overview", problems: {}, message: null, restartNeeded: false, radarInfo: {}, zones: {}, scan: { busy: false, list: null, error: "" }, log: { next: 0, text: "" }, busy: false, rebooting: false,
  github: { busy: false, checked: false, available: false, latest: "", error: "", installing: false },
};
const isAdmin = () => S.session && S.session.role === "admin";

async function api(method, path, body) {
  let response;
  try {
    response = await fetch("/api/v1/" + path, {
      method, credentials: "same-origin",
      headers: Object.assign({ "X-Requested-With": "armor" }, body !== undefined ? { "Content-Type": "application/json" } : {}),
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
  } catch (error) { return { ok: false, status: 0, data: { error: "network" } }; }
  let data = {};
  try { data = await response.json(); } catch (error) { /* an empty answer */ }
  if (response.status === 401 && S.session && S.session.authenticated) { S.session.authenticated = false; start(); }
  return { ok: response.ok, status: response.status, data };
}

const errorText = code => (L["e_" + code] ? t("e_" + code) : String(code || "?"));
const problemText = code => (L["p_" + code] ? t("p_" + code) : String(code));

function pickLanguage(preferred) {
  let code = "";
  try { code = localStorage.getItem("armor_lang") || ""; } catch (error) { /* storage may be blocked */ }
  if (!code) code = preferred || (navigator.language || "en").slice(0, 2);
  const index = LANGS.findIndex(l => l[0] === code);
  lang = index >= 0 ? index : 0;
  document.documentElement.lang = LANGS[lang][0];
}

// ---- the working copy of the settings -----------------------------------------------------------------------------------------------

const walk = (path, create) => {
  const parts = path.split(".");
  let obj = S.cfg;
  for (let i = 0; i < parts.length - 1; ++i) {
    const key = /^\d+$/.test(parts[i]) ? Number(parts[i]) : parts[i];
    if (obj[key] === undefined && create) obj[key] = {};
    obj = obj[key];
  }
  const last = parts[parts.length - 1];
  return [obj, /^\d+$/.test(last) ? Number(last) : last];
};
const getValue = path => { const [o, k] = walk(path); return o ? o[k] : undefined; };
function setValue(path, value) {
  const [o, k] = walk(path, true);
  o[k] = value;
  delete S.problems[path];
  refreshBar();
}
const isDirty = () => S.cfg && JSON.stringify(S.cfg) !== S.saved;

function field(labelKey, path, options = {}) {
  const current = getValue(path);
  const problem = S.problems[path];
  const disabled = !isAdmin() || options.disabled;
  let input;
  if (options.type === "checkbox") {
    input = el("input", { type: "checkbox", checked: !!current, disabled, onchange: e => { setValue(path, e.target.checked); if (options.rerender) render(); } });
    return el("label", { class: "check", tip: labelKey }, input, t(labelKey), problem ? el("span", { class: "err" }, problemText(problem)) : null);
  }
  if (options.type === "select") {
    input = el("select", { disabled, onchange: e => { const v = e.target.value; setValue(path, options.number ? Number(v) : v); if (options.after) options.after(v); if (options.rerender) render(); } },
      options.options.map(([value, text]) => el("option", { value: String(value), selected: String(current) === String(value) }, text)));
  } else if (options.type === "number") {
    input = el("input", { type: "number", min: options.min, max: options.max, step: options.step || 1, value: current === undefined ? "" : current, disabled,
      oninput: e => setValue(path, e.target.value === "" ? NaN : Number(e.target.value)) });
  } else if (options.type === "password") {
    const stored = getValue(path.replace(/password$/, "password_set"));
    input = el("input", { type: "password", value: "", autocomplete: "new-password", disabled, placeholder: stored ? "••••••••" : "", oninput: e => setValue(path, e.target.value) });
  } else {
    input = el("input", { type: "text", value: current === undefined ? "" : current, disabled, maxLength: options.max, placeholder: options.placeholder || "", oninput: e => setValue(path, e.target.value) });
  }
  if (problem) input.classList.add("bad");
  return el("label", { class: "field", tip: labelKey }, el("span", {}, t(labelKey)), input, problem ? el("span", { class: "err" }, problemText(problem)) : null, options.hint ? el("span", { class: "hint" }, options.hint) : null);
}

// ---- the save bar --------------------------------------------------------------------------------------------------------------------

let barNode = null;
function refreshBar() {
  if (!barNode) return;
  const dirty = isDirty();
  const message = S.message || (dirty ? { kind: "warn", text: t("unsaved") } : S.restartNeeded ? { kind: "warn", text: t("savedRestart") } : null);
  barNode.replaceChildren(...[
    el("span", { class: "msg " + (message ? message.kind : "") }, message ? message.text : ""),
    S.restartNeeded && !dirty ? el("button", { class: "b danger", tip: "restart", onclick: () => reboot() }, t("restart")) : null,
    dirty ? el("button", { class: "b", tip: "discard", onclick: discard }, t("discard")) : null,
    dirty ? el("button", { class: "b primary", tip: "save", disabled: S.busy, onclick: () => save(false) }, t("save")) : null,
    dirty ? el("button", { class: "b danger", tip: "saveRestart", disabled: S.busy, onclick: () => save(true) }, t("saveRestart")) : null].filter(Boolean));
  barNode.hidden = !(message || dirty || S.restartNeeded);
}

// The panel's language also lives in the node's settings (ui.language, which the exported file shows): choosing it here saves it there at once, with
// no restart, instead of leaving the node on the language it was set up in. Only an admin can change settings.
async function persistLanguage() {
  if (!S.cfg || !S.session || !S.session.authenticated || !isAdmin()) return;
  const code = LANGS[lang][0];
  if (S.cfg.ui && S.cfg.ui.language === code) return;
  const r = await api("PUT", "config", { ui: { language: code } });
  if (!r.ok) return;
  S.cfg.ui.language = code;
  try { const saved = JSON.parse(S.saved); saved.ui = Object.assign({}, saved.ui, { language: code }); S.saved = JSON.stringify(saved); } catch (error) { /* the next save sorts it out */ }
  refreshBar();
}

function discard() { S.cfg = JSON.parse(S.saved); S.zones = {}; S.problems = {}; S.message = null; render(); }

// Exports the stored configuration (secrets included, like the flash itself) as a .json file: cloning the Wi-Fi, broker, radars and
// pins of a bench node onto a batch of identical ones, without retyping any of it by hand.
async function exportConfig() {
  const r = await api("GET", "config/export");
  if (!r.ok) { S.message = { kind: "err", text: errorText(r.data.error) }; render(); return; }
  const blob = new Blob([JSON.stringify(r.data.config, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = el("a", { href: url, download: "armor-" + (r.data.config.node ? r.data.config.node.id : "node") + ".json" });
  document.body.appendChild(a); a.click(); a.remove();
  URL.revokeObjectURL(url);
}

// Loads a previously exported file on top of the form (not sent to the node yet): the usual Save/Save and restart bar applies it like
// any other edit. The node keeps its own identity - node.id is never overwritten by an import.
function importConfig(file) {
  const reader = new FileReader();
  reader.onload = () => {
    let parsed;
    try { parsed = JSON.parse(String(reader.result)); } catch (error) { S.message = { kind: "err", text: t("importBadFile") }; render(); return; }
    const keepId = S.cfg.node ? S.cfg.node.id : undefined;
    S.cfg = Object.assign({}, S.cfg, parsed, { node: Object.assign({}, S.cfg.node, parsed.node, { id: keepId }) });
    S.zones = {}; S.problems = {}; S.message = { kind: "warn", text: t("importLoaded") };
    render();
  };
  reader.onerror = () => { S.message = { kind: "err", text: t("importBadFile") }; render(); };
  reader.readAsText(file);
}

async function save(restartAfter) {
  S.busy = true; S.message = { kind: "warn", text: t("working") }; refreshBar();
  const r = await api("PUT", "config", S.cfg);
  S.busy = false;
  if (r.ok) {
    S.problems = {};
    await loadConfig();
    S.restartNeeded = S.restartNeeded || !!r.data.restart_required;
    S.message = { kind: "ok", text: r.data.restart_required ? t("savedRestart") : t("saved") };
    if (restartAfter && S.restartNeeded) { reboot(); return; }
  } else if (r.status === 422 && r.data.problems) {
    S.problems = {};
    r.data.problems.forEach(p => { S.problems[p.path] = p.code; });
    S.message = { kind: "bad", text: t("e_invalid") };
  } else S.message = { kind: "bad", text: errorText(r.data.error) };
  render();
}

async function loadConfig() {
  const r = await api("GET", "config");
  if (!r.ok) return;
  S.cfg = r.data.config; S.saved = JSON.stringify(S.cfg); S.channelAuto = r.data.channel_auto; S.firmware = r.data.firmware;
}

async function reboot() {
  await api("POST", "reboot", {});
  S.rebooting = true; render();
  const wait = async () => {
    const r = await api("GET", "session");
    if (r.ok) location.reload(); else setTimeout(wait, 2000);
  };
  setTimeout(wait, 4000);
}

// ---- pages -----------------------------------------------------------------------------------------------------------------------------

const PAGES = [
  { id: "overview", icon: "◎", label: "navOverview", group: "navNode" },
  { id: "network", icon: "⇄", label: "navNetwork", group: "navNetwork" },
  { id: "wifi", icon: "◌", label: "navWifi", group: "navNetwork" },
  { id: "broker", icon: "⌁", label: "navBroker", group: "navNetwork" },
  { id: "radars", icon: "◉", label: "navRadars", group: "navSensors" },
  { id: "map", icon: "⊡", label: "navMap", group: "navSensors" },
  { id: "pins", icon: "▦", label: "navPins", group: "navSensors" },
  { id: "users", icon: "☺", label: "navUsers", group: "navSystem" },
  { id: "update", icon: "⟳", label: "navUpdate", group: "navSystem" },
  { id: "help", icon: "?", label: "navHelp", group: "navSystem" },
  { id: "about", icon: "ⓘ", label: "navAbout", group: "navSystem" },
];

const card = (title, ...kids) => el("section", { class: "card" }, title ? el("h2", {}, title) : null, ...kids);
const kv = rows => el("dl", { class: "kv" }, rows.filter(Boolean).flatMap(([k, v]) => [el("dt", {}, k), el("dd", {}, v)]));
const note = (text, kind) => el("p", { class: "note " + (kind || "") }, text);

const fmtBytes = b => b >= 1048576 ? (Math.round(b / 104857.6) / 10) + " MB" : b >= 1024 ? Math.round(b / 1024) + " kB" : b + " B";
const utcLabel = minutes => "UTC" + (minutes < 0 ? "-" : "+") + String(Math.floor(Math.abs(minutes) / 60)).padStart(2, "0") + ":" + String(Math.abs(minutes) % 60).padStart(2, "0");

// Common zones as POSIX rules (summer time changes by itself); anything else can be typed in as its own rule.
const TIME_ZONES = [
  ["UTC0", "UTC"], ["WET0WEST,M3.5.0/1,M10.5.0", "Lisbon · London · Dublin"], ["CET-1CEST,M3.5.0,M10.5.0/3", "Madrid · Paris · Berlin · Rome (CET/CEST)"],
  ["EET-2EEST,M3.5.0/3,M10.5.0/4", "Athens · Helsinki · Kyiv (EET/EEST)"], ["MSK-3", "Moscow · Istanbul"], ["GMT0", "Reykjavik · Dakar"],
  ["<-03>3", "Buenos Aires · São Paulo"], ["EST5EDT,M3.2.0,M11.1.0", "New York · Toronto"], ["CST6CDT,M3.2.0,M11.1.0", "Chicago · Mexico City"],
  ["MST7MDT,M3.2.0,M11.1.0", "Denver"], ["PST8PDT,M3.2.0,M11.1.0", "Los Angeles · Vancouver"], ["<-05>5", "Bogotá · Lima"], ["<-04>4", "Caracas · La Paz"],
  ["GST-4", "Dubai"], ["IST-5:30", "India"], ["<+07>-7", "Bangkok · Jakarta"], ["CST-8", "Beijing · Singapore · Hong Kong"], ["JST-9", "Tokyo · Seoul"],
  ["AEST-10AEDT,M10.1.0,M4.1.0/3", "Sydney · Melbourne"], ["NZST-12NZDT,M9.5.0,M4.1.0/3", "Auckland"], ["<+02>-2", "Cairo · Johannesburg"],
];
const zoneName = rule => { const hit = TIME_ZONES.find(z => z[0] === rule); return hit ? hit[1] + " (" + rule + ")" : rule; };

function flashCard(flash) {
  const parts = flash.partitions || [];
  const rows = [[t("flashTotal"), fmtBytes(flash.total)], [t("flashAllocated"), fmtBytes(flash.allocated) + " (" + Math.round(flash.allocated * 100 / flash.total) + " %)"], [t("flashUnallocated"), fmtBytes(flash.total - flash.allocated)]];
  parts.forEach(p => {
    const label = p.label.toUpperCase();
    if (p.app) {
      const state = p.running ? " · " + t("slotRunning") : p.next_boot ? " · " + t("slotNextBoot") : "";
      rows.push([label, p.used ? fmtBytes(p.used) + " / " + fmtBytes(p.size) + " (" + Math.round(p.used * 100 / p.size) + " %) · " + fmtBytes(p.size - p.used) + " " + t("flashFree") + " · v" + p.version + state : t("slotEmpty") + " · " + fmtBytes(p.size)]);
    } else rows.push([label, fmtBytes(p.size)]);
  });
  return card(t("ovFlash"), kv(rows));
}

function formatUptime(seconds) {
  const d = Math.floor(seconds / 86400), h = Math.floor(seconds % 86400 / 3600), m = Math.floor(seconds % 3600 / 60);
  return (d ? d + " d " : "") + (h || d ? h + " h " : "") + m + " min";
}
const reasonText = code => (L["reason" + code.charAt(0).toUpperCase() + code.slice(1)] ? t("reason" + code.charAt(0).toUpperCase() + code.slice(1)) : t("reasonOther"));

function radarStateText(state) { return t("state_" + state); }
function statePill(state) { return el("span", { class: "pill " + (state === "reporting" ? "ok" : state === "disabled" ? "" : state === "silent" || state === "garbled" ? "warn" : "bad") }, radarStateText(state).split(":")[0]); }

function overviewPage() {
  const s = S.status;
  if (!s) return el("p", { class: "muted" }, t("loading"));
  const n = s.network, m = s.mqtt;
  const layoutNote = n.ap_setup ? t("setupNetwork") : n.ap_active ? (n.ap_bridged ? t("bridged") : t("ownNetwork")) : "";
  return el("div", { class: "grid" },
    card(t("ovNode"), kv([[t("nodeId"), s.node_id], [t("nodeName"), s.name], [t("firmware"), s.version + " (" + s.partition + ")"], [t("uptime"), formatUptime(s.uptime_s)],
      [t("resetReason"), reasonText(s.reset_reason)], [t("memory"), Math.round(s.heap_free / 1024) + " kB" + (s.psram_free ? " + " + Math.round(s.psram_free / 1048576 * 10) / 10 + " MB PSRAM" : "")],
      [t("light"), s.lux === null ? t("unknown") : s.lux + " lx"]])),
    s.hardware ? card(t("ovHardware"), kv([[t("chip"), s.hardware.chip.toUpperCase() + " rev " + s.hardware.chip_revision + " · " + s.hardware.cores + " " + t("cores")],
      [t("flash"), s.hardware.flash_mb + " MB"], [t("psram"), s.hardware.psram_mb ? s.hardware.psram_mb + " MB" : t("none")],
      [t("appIdf"), s.hardware.app_idf], [t("bootloaderIdf"), s.hardware.bootloader_idf]])) : null,
    s.time ? card(t("ovTime"), kv([[t("localTime"), s.time.set ? s.time.local + " (" + utcLabel(s.time.utc_offset_min) + ")" : t("clockNotSet")],
      [t("timeSource"), !s.time.set ? "—" : s.time.synced ? t("timeFromNtp") : s.time.ntp ? t("timeNtpWaiting") : t("timeManual")], [t("timeZone"), zoneName(s.time.zone)]])) : null,
    s.flash ? flashCard(s.flash) : null,
    card(t("ovNetwork"), kv([[t("board"), BOARD_NAMES[n.board] || n.board || "—"], [t("layout"), n.layout], [t("link"), n.link_up ? t("linkUp") : t("linkDown")], [t("address"), n.has_ip ? n.ip : "—"], [t("netmask"), n.netmask || "—"], [t("gateway"), n.gateway || "—"],
      [t("dns"), n.dns || "—"], [t("mac"), n.mac],
      n.ap_active ? [t("apActive"), "“" + n.ap_ssid + "” · " + t("channel") + " " + n.ap_channel + " · " + n.ap_clients + " " + t("apClients") + " · " + layoutNote] : null,
      n.sta_ssid ? [t("station"), n.sta_ssid + (n.sta_connected ? " · " + n.sta_rssi + " dBm" : " · " + t("notConnected"))] : null]),
      n.ethernet_available === false || n.ethernet_ok ? null : note(t("ethernetMissing"), "bad")),
    card(t("ovBroker"), kv([[t("navBroker"), !m.enabled ? t("notConfigured") : m.connected ? t("connected") : t("notConnected")], [t("clock"), m.clock_set ? t("clockSet") : t("clockNotSet")], [t("messages"), m.published]]),
      m.withheld === "light" ? note(t("withheldLight")) : m.withheld === "radars" ? note(t("withheldRadars")) : null),
    card(t("radarsTitle"), el("div", { class: "row" }, s.radars.map(r => el("div", {}, el("h3", {}, t("sensorN", r.radar), r.enabled && r.model ? " · " + modelOf(r.model).label : "", r.enabled && r.name ? " · " + r.name : ""), statePill(r.enabled ? r.state : "disabled"),
      r.enabled ? el("p", { class: "muted" }, presenceText(r) ? presenceText(r) + " · " : "", t("framesRate", r.fps), " · ", t("counters", r.bytes, r.frames, r.bad_frames)) : null)))));
}

// The board the image is for: the s3-eth board has an Ethernet port, the s3-wifi board has none (its way in is always Wi-Fi).
const hasEthernet = () => !(S.session && S.session.ethernet === false);
const BOARD_NAMES = { "s3-eth": "Waveshare ESP32-S3-ETH", "s3-wifi": "ESP32-S3-WROOM-1 N16R8" };

function networkPage() {
  const cfg = S.cfg;
  return el("div", { class: "grid wide" },
    card(t(hasEthernet() ? "netTitle" : "nodeTitle"),
      hasEthernet() ? field("uplink", "uplink", { type: "select", rerender: true, options: [["ethernet", t("uplinkEthernet")], ["wifi", t("uplinkWifi")]] }) : null,
      field("dhcp", "ip.dhcp", { type: "checkbox", rerender: true }),
      !cfg.ip.dhcp ? el("div", { class: "row" }, field("address", "ip.address"), field("netmask", "ip.netmask"), field("gateway", "ip.gateway"), field("dns1", "ip.dns1"), field("dns2", "ip.dns2")) : null,
      field("hostname", "ip.hostname", { hint: t("hostnameHint") }),
      field("nodeName", "node.name"), field("nodeId", "node.id"),
      note(t("netRestartNote"), "info")),
    card(t("webTitle"), field("webMode", "web.mode", { type: "select", options: [["both", t("webBoth")], ["https", t("webHttps")], ["http", t("webHttp")]] }),
      S.status && S.status.web ? el("p", { class: "muted" }, t(S.status.web.https ? "webRunning" : "webNotRunning")) : null,
      S.status && S.status.web && S.status.web.cert_sha256 ? el("p", { class: "muted mono" }, t("webFingerprint") + ": " + S.status.web.cert_sha256) : null, note(t("webNote"), "info")),
    card(t("clockTitle"),
      field("timeZone", "time.zone", { type: "select", options: TIME_ZONES.some(z => z[0] === S.cfg.time.zone) ? TIME_ZONES.map(z => z) : [[S.cfg.time.zone, S.cfg.time.zone]].concat(TIME_ZONES) }),
      field("ntpEnabled", "time.ntp_enabled", { type: "checkbox", rerender: true }),
      cfg.time.ntp_enabled ? field("ntp", "time.ntp") : el("div", { class: "actions" }, el("button", { class: "b", tip: "setTimeFromBrowser", disabled: !isAdmin(), onclick: setClockFromBrowser }, t("setTimeFromBrowser"))),
      S.status && S.status.time ? el("p", { class: "muted" }, t("localTime") + ": " + (S.status.time.set ? S.status.time.local : t("clockNotSet"))) : null,
      note(t("clockNote"), "info")),
    card(t("bleTitle"), field("bleMode", "ble.mode", { type: "select", options: [["setup", t("bleSetup")], ["always", t("bleAlways")], ["off", t("bleOff")]] }), note(t("bleNote"), "info")),
    card(t("systemTitle"), field("autoRestart", "system.auto_restart_hours", { type: "select", number: true,
      options: [[0, t("autoRestartNever")], [1, t("autoRestart1")], [2, t("autoRestart2")], [3, t("autoRestart3")], [4, t("autoRestart4")], [6, t("autoRestart6")], [12, t("autoRestart12")], [24, t("autoRestart24")], [48, t("autoRestart48")]] }),
      note(t("autoRestartNote"), "info")),
    isAdmin() ? card(t("configBackupTitle"),
      el("div", { class: "row" },
        el("button", { class: "b", tip: "exportConfig", onclick: exportConfig }, t("exportConfig")),
        el("label", { class: "b", tip: "importConfig" }, t("importConfig"), el("input", { type: "file", accept: "application/json", hidden: true, onchange: e => { if (e.target.files[0]) importConfig(e.target.files[0]); e.target.value = ""; } }))),
      note(t("configBackupNote"), "info")) : null);
}

async function scanNetworks() {
  S.scan = { busy: true, list: null, error: "" }; render();
  const r = await api("GET", "wifi/scan");
  S.scan = r.ok ? { busy: false, list: r.data.networks || [], error: "" } : { busy: false, list: null, error: errorText(r.data.error) };
  render();
}

function scanResults() {
  const scan = S.scan;
  if (scan.busy) return el("p", { class: "muted" }, t("scanning"));
  if (scan.error) return el("p", { class: "err" }, scan.error);
  if (!scan.list) return null;
  if (!scan.list.length) return el("p", { class: "muted" }, t("noNetworks"));
  return el("div", { class: "scroll" }, el("table", {}, el("thead", {}, el("tr", {}, el("th", {}, t("ssid")), el("th", {}, t("signal")), el("th", {}, t("wifiChannel")), el("th", {}, t("security")), el("th", {}))),
    el("tbody", {}, scan.list.map(n => el("tr", {}, el("td", {}, n.ssid), el("td", { class: "mono" }, n.rssi + " dBm"), el("td", {}, n.channel), el("td", {}, n.security),
      el("td", {}, el("button", { class: "b", disabled: !isAdmin(), onclick: () => { setValue("sta.enabled", true); setValue("sta.ssid", n.ssid); render(); } }, t("useNetwork"))))))));
}

function wifiPage() {
  const cfg = S.cfg, ap = cfg.ap;
  const channels = [[0, t("channelAuto")]].concat(Array.from({ length: 13 }, (_, i) => [i + 1, String(i + 1)]));
  return el("div", { class: "grid wide" },
    card(t("wifiAp"),
      field("apEnable", "ap.enabled", { type: "checkbox", rerender: true }),
      ap.enabled ? [
        el("div", { class: "row" }, field("ssid", "ap.ssid", { max: 32 }),
          field("security", "ap.security", { type: "select", rerender: true, options: [["wpa2", t("secWpa2")], ["wpa3", t("secWpa3")], ["wpa2wpa3", t("secWpa2Wpa3")], ["open", t("secOpen")]] })),
        ap.security !== "open" ? field("wifiPassword", "ap.password", { type: "password" }) : null,
        el("div", { class: "row" },
          field("wifiChannel", "ap.channel", { type: "select", number: true, options: channels, hint: ap.channel === 0 ? t("channelNow", S.channelAuto) : "" }),
          field("maxClients", "ap.max_clients", { type: "number", min: 1, max: 10 }), field("txPower", "ap.tx_power_dbm", { type: "number", min: 2, max: 20 }),
          field("bandwidth", "ap.bandwidth_mhz", { type: "select", number: true, options: [[20, t("bw20")], [40, t("bw40")]] }), field("country", "ap.country", { max: 2 })),
        field("hidden", "ap.hidden", { type: "checkbox" }),
        hasEthernet() && cfg.uplink === "ethernet" ? field("bridge", "ap.bridge", { type: "checkbox" }) : null,
        note(t("meshNote"), "info")] : null),
    card(t("wifiSta"), field("staEnable", "sta.enabled", { type: "checkbox", rerender: true }),
      cfg.sta.enabled ? [el("div", { class: "row" }, field("ssid", "sta.ssid", { max: 32 }), field("wifiPassword", "sta.password", { type: "password" }))] : null,
      el("div", { class: "actions" }, el("button", { class: "b", tip: "scanNetworks", disabled: !isAdmin() || S.scan.busy, onclick: scanNetworks }, t("scanNetworks"))), scanResults(),
      el("p", { class: "hint" }, t("scanNote")), note(t("staNote"), "info"),
      cfg.sta.enabled ? backupNetworks() : null));
}

const MAX_BACKUP_NETWORKS = 3;
function backupNetworks() {
  const list = S.cfg.sta.backup;
  return el("div", {}, el("h3", {}, t("backupNetworksTitle")), el("p", { class: "hint" }, t("backupNetworksHint")),
    list.map((network, i) => {
      const base = "sta.backup." + i + ".";
      return el("div", { class: "row" }, field("ssid", base + "ssid", { max: 32 }), field("wifiPassword", base + "password", { type: "password" }),
        el("button", { class: "b danger", tip: "removeNetwork", disabled: !isAdmin(), onclick: () => { list.splice(i, 1); refreshBar(); render(); } }, t("removeNetwork")));
    }),
    isAdmin() && list.length < MAX_BACKUP_NETWORKS
      ? el("div", { class: "actions" }, el("button", { class: "b", tip: "addBackupNetwork", onclick: () => { list.push({ ssid: "", password: "" }); refreshBar(); render(); } }, t("addBackupNetwork")))
      : null);
}

const MAX_BACKUP_BROKERS = 2;
function backupBrokers() {
  const list = S.cfg.mqtt.backup;
  return el("div", {}, el("h3", {}, t("backupBrokersTitle")), el("p", { class: "hint" }, t("backupBrokersHint")),
    list.map((broker, i) => {
      const base = "mqtt.backup." + i + ".";
      return el("div", {}, el("div", { class: "row" }, field("brokerUri", base + "uri", { placeholder: "mqtt://192.168.0.180:18883" }),
        el("button", { class: "b danger", tip: "removeNetwork", disabled: !isAdmin(), onclick: () => { list.splice(i, 1); refreshBar(); render(); } }, t("removeNetwork"))),
        el("div", { class: "row" }, field("brokerUser", base + "username"), field("brokerPassword", base + "password", { type: "password" })));
    }),
    isAdmin() && list.length < MAX_BACKUP_BROKERS
      ? el("div", { class: "actions" }, el("button", { class: "b", tip: "addBackupBroker", onclick: () => { list.push({ uri: "", username: "", password: "" }); refreshBar(); render(); } }, t("addBackupBroker")))
      : null);
}

function brokerPage() {
  const cfg = S.cfg, mq = cfg.mqtt;
  return el("div", { class: "grid wide" },
    card(t("brokerTitle"), field("brokerEnable", "mqtt.enabled", { type: "checkbox", rerender: true }),
      mq.enabled ? [field("brokerUri", "mqtt.uri", { placeholder: "mqtt://192.168.0.180:18883" }),
        el("div", { class: "row" }, field("brokerUser", "mqtt.username"), field("brokerPassword", "mqtt.password", { type: "password" })),
        el("div", { class: "row" }, field("heartbeat", "mqtt.heartbeat_s", { type: "number", min: 2, max: 300 }), field("telemetryMs", "mqtt.telemetry_ms", { type: "number", min: 200, max: 5000, step: 100 })),
        backupBrokers()] : null,
      note(t("brokerNote"), "info")),
    card(t("sensorsTitle"), field("veml", "sensors.veml7700", { type: "checkbox", rerender: true }),
      cfg.sensors.veml7700 ? el("div", { class: "row" }, field("sda", "sensors.sda", { type: "number", min: 0, max: 48 }), field("scl", "sensors.scl", { type: "number", min: 0, max: 48 })) : null,
      field("luxFallback", "sensors.lux_fallback", { type: "number", min: -1, max: 200000 }), note(t("luxNote"))));
}

// ---- pins and radars ------------------------------------------------------------------------------------------------------------------

function pinLabel(info) {
  return "GPIO " + info.gpio + (info.use === "caution" ? " ⚠" : info.use === "sd" ? " (microSD)" : "") + (info.on_header ? "" : " *");
}
// Every GPIO this draft config already claims (an enabled radar's rx/tx, the light sensor's sda/scl, an enabled mapped pin),
// except the one field at `exceptPath` - so a dropdown doesn't grey out the value it is itself showing.
function claimedGpios(exceptPath) {
  const used = new Set();
  const add = (path, gpio) => { if (path !== exceptPath && gpio >= 0) used.add(gpio); };
  S.cfg.pins.forEach((p, i) => { if (p.mode !== "disabled") add("pins." + i + ".gpio", p.gpio); });
  S.cfg.radars.forEach((r, i) => { if (r.enabled) { add("radars." + i + ".rx", r.rx); add("radars." + i + ".tx", r.tx); } });
  if (S.cfg.sensors.veml7700) { add("sensors.sda", S.cfg.sensors.sda); add("sensors.scl", S.cfg.sensors.scl); }
  return used;
}
function pinOptions(selected, includeNone, exceptPath) {
  const used = exceptPath !== undefined ? claimedGpios(exceptPath) : new Set();
  const assignable = S.catalog.filter(p => p.use !== "reserved" && (p.gpio === selected || !used.has(p.gpio)));
  const list = assignable.map(p => [p.gpio, pinLabel(p)]);
  if (selected >= 0 && !assignable.some(p => p.gpio === selected)) list.unshift([selected, "GPIO " + selected]);
  return (includeNone ? [[-1, t("none")]] : []).concat(list);
}

const REPORTS = { input: ["triggered", "open", "on", "tamper"], output: ["on", "locked"], pwm: ["brightness"], adc: ["mv", "battery", "brightness", "humidity", "temperature", "lux", "power_w", "co_ppm"] };
const DEFAULT_REPORT = { input: "triggered", output: "on", pwm: "brightness", adc: "mv" };

function pinCard(index) {
  const pin = S.cfg.pins[index], base = "pins." + index + ".";
  const live = S.live.find(l => l.name === pin.name);
  const modeChanged = e => {
    const mode = e.target.value;
    setValue(base + "mode", mode);
    const old = REPORTS[pin.mode] || [];
    if (mode !== "disabled" && (!pin.report || !(REPORTS[mode] || []).includes(pin.report) || old.indexOf(pin.report) < 0)) pin.report = DEFAULT_REPORT[mode] || "";
    render();
  };
  const modeSelect = el("select", { disabled: !isAdmin(), onchange: modeChanged },
    [["disabled", "modeDisabled"], ["input", "modeInput"], ["output", "modeOutput"], ["pwm", "modePwm"], ["adc", "modeAdc"]].map(([v, k]) => el("option", { value: v, selected: pin.mode === v }, t(k))));
  const parts = [
    el("div", { class: "row" }, field("pinGpio", base + "gpio", { type: "select", number: true, options: pinOptions(pin.gpio, false, base + "gpio") }), field("pinName", base + "name", { max: 24 }),
      el("label", { class: "field" }, el("span", {}, t("pinMode")), modeSelect)),
  ];
  const mode = pin.mode;
  if (mode === "input") parts.push(el("div", { class: "row" }, field("pull", base + "pull", { type: "select", options: [["none", t("pullNone")], ["up", t("pullUp")], ["down", t("pullDown")]] }),
    field("reportAs", base + "report", { type: "select", options: REPORTS.input.map(r => [r, r]) }), field("debounceMs", base + "debounce_ms", { type: "number", min: 0, max: 5000 })), field("invert", base + "invert", { type: "checkbox" }));
  if (mode === "output") parts.push(el("div", { class: "row" }, field("reportAs", base + "report", { type: "select", options: REPORTS.output.map(r => [r, r]) }),
    field("safeState", base + "safe", { type: "select", options: [["keep", t("safeKeep")], ["off", t("safeOff")], ["on", t("safeOn")]] }), field("linkTimeout", base + "link_timeout_s", { type: "number", min: 0, max: 86400 }),
    field("pulseMs", base + "pulse_ms", { type: "number", min: 0, max: 3600000 })), el("div", { class: "row" }, field("initialOn", base + "initial_on", { type: "checkbox" }), field("invert", base + "invert", { type: "checkbox" })));
  if (mode === "pwm") parts.push(el("div", { class: "row" }, field("freqHz", base + "freq_hz", { type: "number", min: 1, max: 40000 })), el("div", { class: "row" }, field("initialOn", base + "initial_on", { type: "checkbox" }), field("invert", base + "invert", { type: "checkbox" })));
  if (mode === "adc") parts.push(el("div", { class: "row" }, field("reportAs", base + "report", { type: "select", options: REPORTS.adc.map(r => [r, r]) }), field("periodS", base + "period_s", { type: "number", min: 1, max: 3600 }),
    field("scale", base + "scale", { type: "number", step: "any" }), field("offset", base + "offset", { type: "number", step: "any" })));
  if (mode !== "disabled" && pin.name && S.status) {
    const id = S.status.node_id;
    const stateTopic = "armor/device/" + id + "/" + pin.name + "/state", setTopic = "armor/device/" + id + "/" + pin.name + "/set";
    const box = el("div", { class: "muted mono" }, t("topicState") + ": " + stateTopic, mode === "output" || mode === "pwm" ? el("br") : null, mode === "output" || mode === "pwm" ? t("topicSet") + ": " + setTopic : null);
    parts.push(box);
    if (live) {
      const buttons = [];
      if (mode === "output") for (const [word, key] of [["on", "cmdOn"], ["off", "cmdOff"], ["toggle", "cmdToggle"], ["pulse", "cmdPulse"]]) buttons.push(el("button", { class: "b", disabled: !isAdmin(), onclick: () => pinCommand(pin.name, word) }, t(key)));
      if (mode === "pwm") { const box2 = el("input", { type: "number", min: 0, max: 100, value: live.percent }); buttons.push(box2, el("button", { class: "b", disabled: !isAdmin(), onclick: () => pinCommand(pin.name, String(box2.value)) }, "%")); }
      parts.push(el("div", { class: "actions" }, el("span", { class: "pill " + (live.on ? "ok" : "") }, t("liveState") + ": " + (mode === "adc" ? live.value : mode === "pwm" ? live.percent + " %" : live.on ? t("stateOn") : t("stateOff"))),
        live.fell_back ? el("span", { class: "pill warn" }, t("fellBack")) : null, buttons));
    }
  }
  return el("section", { class: "card" }, parts, isAdmin() ? el("div", { class: "actions" }, el("button", { class: "b danger", onclick: () => { S.cfg.pins.splice(index, 1); refreshBar(); render(); } }, t("remove"))) : null);
}

async function pinCommand(name, command) {
  const r = await api("POST", "pins/command", { name, command });
  S.message = r.ok ? null : { kind: "bad", text: errorText(r.data.error) };
  await refreshLive(); render();
}

function pinsPage() {
  const pins = S.cfg.pins;
  return el("div", {}, note(t(hasEthernet() ? "pinsWarn" : "pinsWarnWifi")), el("p", { class: "muted" }, t("pinsIntro")), note(t("pinsRestart"), "info"),
    el("div", { class: "grid wide" }, pins.length ? pins.map((_, i) => pinCard(i)) : [el("p", { class: "muted" }, t("noPins"))]),
    isAdmin() && pins.length < 16 ? el("div", { class: "actions" }, el("button", { class: "b primary", tip: "addPin", onclick: addPin }, t("addPin"))) : null);
}

function addPin() {
  const used = new Set(S.cfg.pins.map(p => p.gpio).concat(S.cfg.radars.flatMap(r => [r.rx, r.tx]), [S.cfg.sensors.sda, S.cfg.sensors.scl]));
  const free = S.catalog.find(p => p.use === "free" && !used.has(p.gpio));
  S.cfg.pins.push({ gpio: free ? free.gpio : -1, name: "pin" + (S.cfg.pins.length + 1), mode: "output", invert: false, pull: "none", initial_on: false, safe: "keep", link_timeout_s: 0, pulse_ms: 0,
    debounce_ms: 30, period_s: 10, freq_hz: 1000, report: "on", scale: 1, offset: 0 });
  refreshBar(); render();
}

async function radarCommand(index, op, extra) {
  S.radarInfo[index] = { busy: true }; render();
  const r = await api("POST", "radars/command", Object.assign({ radar: index + 1, op }, extra || {}));
  const d = r.data;
  S.radarInfo[index] = { ok: !!d.ok, error: d.error || (r.ok ? "" : d.error), firmware: d.firmware, mode: d.tracking_mode, last: d.last_answer, done: !!d.ok };
  if (d.zones) S.zones[index] = { type: d.zones.type, list: d.zones.list };
  if (d.ok) {
    // what the sensor was just told is also kept in the node's settings (and so in the exported file), and told to the sensor again at every start
    if (op === "set_zones" && extra && extra.zones) persistRadar(index, { target_mode: undefined, zones: { type: Number(extra.zones.type) || 0, list: extra.zones.list.map(z => ({ x1: z.x1 | 0, y1: z.y1 | 0, x2: z.x2 | 0, y2: z.y2 | 0 })) } });
    else if (op === "single") persistRadar(index, { target_mode: 1 });
    else if (op === "multi") persistRadar(index, { target_mode: 2 });
    else if (op === "factory") persistRadar(index, { target_mode: 0, zones: { type: 0, list: [0, 1, 2].map(() => ({ x1: 0, y1: 0, x2: 0, y2: 0 })) } });
  }
  await refreshLive(); render();
}

// Saves a few settings of one radar at once, without touching the rest of the form (the node merges a partial document), and keeps the form's
// own copy of what is saved in step so the form is not left marked as changed.
async function persistRadar(index, patch) {
  const clean = Object.fromEntries(Object.entries(patch).filter(([, v]) => v !== undefined));
  const radars = [0, 1, 2].slice(0, index + 1).map(i => (i === index ? clean : {}));
  const r = await api("PUT", "config", { radars });
  if (!r.ok) return;
  Object.assign(S.cfg.radars[index], clean);
  try { const saved = JSON.parse(S.saved); Object.assign(saved.radars[index], clean); S.saved = JSON.stringify(saved); } catch (error) { /* the next save sorts it out */ }
}

// What each model can be told (the node answers "unsupported" to the rest) and how its serial port starts.
const SENSOR_MODELS = [
  { id: "ld2450", label: "HLK-LD2450", tracker: true, baud: 256000, cmds: ["read_info", "single", "multi", "bluetooth", "restart", "factory", "zones"] },
  { id: "ld2461", label: "HLK-LD2461", tracker: true, baud: 9600, cmds: ["read_info", "factory", "zones"] },
  { id: "ld2410", label: "HLK-LD2410B / LD2410C", tracker: false, baud: 256000, cmds: ["read_info", "bluetooth", "restart", "factory"] },
  { id: "ld2412", label: "HLK-LD2412", tracker: false, baud: 115200, cmds: ["read_info", "bluetooth", "restart", "factory"] },
  { id: "ld2410s", label: "HLK-LD2410S", tracker: false, baud: 115200, cmds: [] },
  { id: "mr24hpc1", label: "Seeed MR24HPC1", tracker: false, baud: 9600, cmds: [] },
];
const SENSOR_BAUDS = [9600, 19200, 38400, 57600, 115200, 230400, 256000, 460800];
const modelOf = id => SENSOR_MODELS.find(m => m.id === id) || SENSOR_MODELS[0];

function presenceText(r) {
  if (r.tracker !== false) return "";
  if (r.present === undefined || r.present < 0) return t("presenceUnknown");
  return t(r.present ? "presenceYes" : "presenceNo") + (r.present && r.distance_cm >= 0 ? " " + t("distanceCm", r.distance_cm) : "");
}
function liveText(r) {
  const presence = presenceText(r);
  return (presence ? presence + " · " : "") + radarStateText(r.state) + " · " + t("framesRate", r.fps) + " · " + t("counters", r.bytes, r.frames, r.bad_frames);
}
// The numbers a model adds to its report: the zones of an LD2461, the energies of an LD2410, the flags of an MR24.
function detailText(r) {
  const d = r.detail;
  if (!d || r.state === "disabled") return "";
  if (Array.isArray(d.zones)) { const busy = d.zones.map((z, i) => z ? i + 1 : 0).filter(Boolean); return t("zonesOccupied", busy.length ? busy.join(", ") : t("none")); }
  if (d.moving_cm !== undefined) return t("presenceDetail", d.moving_cm, d.moving_energy, d.static_cm, d.static_energy);
  if (d.motion !== undefined) return t("mr24Detail", d.motion, d.body_movement, d.proximity);
  return "";
}

function radarCard(index) {
  const cfg = S.cfg.radars[index], base = "radars." + index + ".";
  const status = (S.status && S.status.radars[index]) || { enabled: false, state: "disabled" };
  const info = S.radarInfo[index] || {};
  const model = modelOf(cfg.model);
  const zone = S.zones[index] || (S.zones[index] = { type: (cfg.zones && cfg.zones.type) || 0, list: [0, 1, 2].map(z => Object.assign({ x1: 0, y1: 0, x2: 0, y2: 0 }, cfg.zones && cfg.zones.list && cfg.zones.list[z])) });
  const canCommand = isAdmin() && status.enabled && status.tx >= 0 && !info.busy && status.model === cfg.model;
  const ask = (action) => { if (confirm(t("confirmAsk"))) action(); };
  const can = name => model.cmds.includes(name);
  const zoneRows = zone.list.map((z, i) => el("div", { class: "zone" }, ["x1", "y1", "x2", "y2"].map(k => el("input", { type: "number", value: z[k], "aria-label": k + " " + (i + 1), disabled: !isAdmin(),
    oninput: e => { z[k] = Number(e.target.value); } }))));
  const button = (cls, name, label, run) => can(name) ? el("button", { class: "b " + cls, disabled: !canCommand, onclick: run }, t(label)) : null;
  const formatNumber = info.mode || status.tracking_mode;
  const modelChanged = value => { if (!modelOf(value).tracker && !cfg.name) setValue(base + "name", "presence" + (index + 1)); };
  return el("section", { class: "card" }, el("h2", {}, t("sensorN", index + 1), cfg.enabled ? " · " + model.label : ""),
    field("radarConnected", base + "enabled", { type: "checkbox", rerender: true }),
    cfg.enabled ? [
      el("div", { class: "row" },
        field("sensorModel", base + "model", { type: "select", rerender: true, after: modelChanged, options: SENSOR_MODELS.map(m => [m.id, m.label]) }),
        field("baudRate", base + "baud", { type: "select", number: true, options: [[0, t("baudModel", model.baud)]].concat(SENSOR_BAUDS.map(b => [b, String(b)])) })),
      el("p", { class: "hint" }, t(model.tracker ? "kindTracker" : "kindPresence")),
      el("div", { class: "row" }, model.tracker ? field("radarLabel", base + "name", { max: 24, hint: t("radarLabelHint") }) : field("deviceName", base + "name", { max: 24, hint: t("deviceNameHint") })),
      el("div", { class: "row" }, field("rxPin", base + "rx", { type: "select", number: true, options: pinOptions(cfg.rx, false, base + "rx") }), field("txPin", base + "tx", { type: "select", number: true, options: pinOptions(cfg.tx, true, base + "tx") })),
      model.tracker ? [
        el("h3", {}, t("calibrationTitle")),
        el("div", { class: "row" },
          field("offsetX", base + "offset_x_mm", { type: "number", min: -5000, max: 5000 }),
          field("offsetY", base + "offset_y_mm", { type: "number", min: -5000, max: 5000 }),
          field("yawDeg", base + "yaw_deg", { type: "number", min: -180, max: 180 }),
          field("pitchDeg", base + "pitch_deg", { type: "number", min: -45, max: 45 })),
        note(t("calibrationNote"), "info")] : null] : null,
    status.enabled ? el("div", { class: "actions" }, statePill(status.state), el("span", { class: "muted", id: "radar-live-" + index }, liveText(status))) : null,
    status.enabled ? [
      el("p", { class: "muted", id: "radar-detail-" + index }, detailText(status)),
      status.model === "ld2461" && status.state === "reporting" && status.detail && status.detail.coordinates === false ? note(t("zonesOnly")) : null,
      status.firmware || info.firmware ? el("p", { class: "muted mono" }, t("firmwareOfModule") + ": " + (info.firmware || status.firmware)) : null,
      formatNumber && can("read_info") ? el("p", { class: "muted" }, t(model.id === "ld2461" ? "reportFormat" : "trackingMode") + ": " + t((model.id === "ld2461" ? "fmt" : "mode") + formatNumber)) : null,
      model.cmds.length === 0 ? el("p", { class: "hint" }, t("noCommands")) : null,
      model.cmds.length ? el("div", { class: "actions" },
        button("primary", "read_info", "readInfo", () => radarCommand(index, "read_info")),
        button("", "single", "singleTarget", () => radarCommand(index, "single")),
        button("", "multi", "multiTarget", () => radarCommand(index, "multi")),
        button("", "bluetooth", "bluetoothOff", () => radarCommand(index, "bluetooth", { flag: false })),
        button("", "bluetooth", "bluetoothOn", () => radarCommand(index, "bluetooth", { flag: true })),
        button("danger", "restart", "restartModule", () => ask(() => radarCommand(index, "restart"))),
        button("danger", "factory", "factoryModule", () => ask(() => radarCommand(index, "factory")))) : null,
      can("zones") ? [
        el("h3", {}, t("zonesTitle")),
        el("label", { class: "field" }, el("span", {}, t("zoneType")), el("select", { disabled: !isAdmin(), onchange: e => { zone.type = Number(e.target.value); } },
          [[0, t("zoneOff")], [1, t("zoneInside")], [2, t("zoneOutside")]].map(([v, text]) => el("option", { value: String(v), selected: zone.type === v }, text)))),
        el("div", { class: "zones" }, zoneRows), el("p", { class: "hint" }, t(model.id === "ld2461" ? "zoneHintLd2461" : "zoneHint")),
        el("div", { class: "actions" }, el("button", { class: "b", disabled: !canCommand, onclick: () => radarCommand(index, "set_zones", { zones: { type: zone.type, list: zone.list } }) }, t("applyZones")))] : null,
      info.busy ? el("p", { class: "muted" }, t("working")) : info.done ? el("p", { class: "hint" }, t("cmdDone")) : info.error ? el("p", { class: "err" }, errorText(info.error)) : null,
      info.last !== undefined ? el("p", { class: "muted mono" }, t("lastAnswer") + ": " + (info.last || "—")) : null] : null);
}

function radarsPage() {
  return el("div", {}, note(t("radarWarn")),
    card(t("fusionTitle"), field("mergeMm", "fusion.merge_mm", { type: "number", min: 0, max: 2000, hint: t("fusionHint") }), note(t("fusionNote"), "info")),
    el("div", { class: "grid wide" }, [0, 1, 2].map(radarCard)));
}

// ---- users and system ------------------------------------------------------------------------------------------------------------------

const usersMessage = { text: "", kind: "" };
async function usersAction(promise, done) {
  const r = await promise;
  usersMessage.text = r.ok ? (done || t("saved")) : errorText(r.data.error); usersMessage.kind = r.ok ? "ok" : "bad";
  const list = await api("GET", "users");
  if (list.ok) S.users = list.data;
  render();
  return r;
}

function usersPage() {
  const form = { name: "", password: "", role: "viewer" }, mine = { current: "", password: "" };
  const rows = S.users.map(u => {
    const pw = el("input", { type: "password", placeholder: t("newPassword"), autocomplete: "new-password" });
    return el("tr", {}, el("td", {}, u.name, u.name === S.session.user ? el("span", { class: "hint" }, " (" + t("you") + ")") : null),
      el("td", {}, el("select", { onchange: e => usersAction(api("PUT", "users/" + u.name, { role: e.target.value })) },
        [["admin", t("roleAdmin")], ["viewer", t("roleViewer")]].map(([v, text]) => el("option", { value: v, selected: u.role === v }, text)))),
      el("td", {}, pw), el("td", { class: "actions" }, el("button", { class: "b", onclick: () => usersAction(api("PUT", "users/" + u.name, { password: pw.value })) }, t("setPassword")),
        el("button", { class: "b danger", onclick: () => confirm(t("confirmAsk")) && usersAction(api("DELETE", "users/" + u.name)) }, t("delete"))));
  });
  return el("div", { class: "grid wide" },
    isAdmin() ? card(t("usersTitle"), el("div", { class: "scroll" }, el("table", {}, el("thead", {}, el("tr", {}, el("th", {}, t("user")), el("th", {}, t("role")), el("th", {}, t("newPassword")), el("th", {}))), el("tbody", {}, rows))),
      el("h3", {}, t("addUser")),
      el("div", { class: "row" }, el("label", { class: "field" }, el("span", {}, t("user")), el("input", { oninput: e => { form.name = e.target.value; } })),
        el("label", { class: "field" }, el("span", {}, t("newPassword")), el("input", { type: "password", autocomplete: "new-password", oninput: e => { form.password = e.target.value; } })),
        el("label", { class: "field" }, el("span", {}, t("role")), el("select", { onchange: e => { form.role = e.target.value; } }, [["viewer", t("roleViewer")], ["admin", t("roleAdmin")]].map(([v, text]) => el("option", { value: v }, text))))),
      el("div", { class: "actions" }, el("button", { class: "b primary", onclick: () => usersAction(api("POST", "users", form)) }, t("add"))),
      usersMessage.text ? el("p", { class: usersMessage.kind === "ok" ? "hint" : "err" }, usersMessage.text) : null) : null,
    card(t("myAccount"),
      el("label", { class: "field" }, el("span", {}, t("currentPassword")), el("input", { type: "password", autocomplete: "current-password", oninput: e => { mine.current = e.target.value; } })),
      el("label", { class: "field" }, el("span", {}, t("newPassword")), el("input", { type: "password", autocomplete: "new-password", oninput: e => { mine.password = e.target.value; } })),
      el("div", { class: "actions" }, el("button", { class: "b primary", onclick: async () => {
        const r = await api("PUT", "account", mine);
        if (r.ok) { alert(t("passwordChanged")); S.session.authenticated = false; start(); } else { usersMessage.text = errorText(r.data.error); usersMessage.kind = "bad"; render(); }
      } }, t("changePassword"))), !isAdmin() && usersMessage.text ? el("p", { class: "err" }, usersMessage.text) : null));
}

function uploadFirmware(file, progressBar, label, done) {
  const request = new XMLHttpRequest();
  request.open("POST", "/api/v1/ota");
  request.setRequestHeader("X-Requested-With", "armor");
  request.upload.onprogress = e => { if (e.lengthComputable) { const pct = Math.round(e.loaded * 100 / e.total); progressBar.style.width = pct + "%"; label.textContent = t("uploading", pct); } };
  request.onload = () => {
    let data = {};
    try { data = JSON.parse(request.responseText); } catch (error) { /* keep {} */ }
    done(request.status === 200 ? { ok: true, version: data.version } : { ok: false, error: data.error || "network" });
  };
  request.onerror = () => done({ ok: false, error: "network" });
  request.send(file);
}

async function setClockFromBrowser() {
  const r = await api("POST", "time", { epoch: Math.floor(Date.now() / 1000) });
  S.message = r.ok ? { kind: "ok", text: t("timeSetOk") } : { kind: "err", text: errorText(r.data.error) };
  await refreshLive(); render();
}

async function githubCheck() {
  S.github.busy = true; S.github.error = ""; render();
  const r = await api("GET", "ota/check");
  S.github.busy = false;
  if (r.ok && r.data.ok) { S.github.checked = true; S.github.available = r.data.update_available; S.github.latest = r.data.latest_version; }
  else { S.github.checked = true; S.github.available = false; S.github.error = errorText((r.data && r.data.error) || "network"); }
  render();
}
async function githubInstall() {
  S.github.installing = true; render();
  const r = await api("POST", "ota/install");
  if (r.ok) { S.rebooting = true; render(); setTimeout(() => { const wait = async () => { const q = await api("GET", "session"); if (q.ok) location.reload(); else setTimeout(wait, 2000); }; wait(); }, 6000); }
  else { S.github.installing = false; S.github.error = errorText(r.data.error); render(); }
}

function aboutPage() {
  const s = S.status;
  return el("div", { class: "grid wide" },
    card(null,
      el("p", { class: "eyebrow" }, "AUTONOMOUS RADAR & MULTIMODAL OBSERVATION RANGE"),
      el("h2", {}, "A.R.M.O.R. Radar"),
      el("p", {}, t("aboutDescription")),
      kv([[t("nodeId"), s ? s.node_id : "—"], [t("nodeName"), s ? s.name : "—"], [t("firmware"), s ? s.version : "—"],
        [t("author"), "JuanenRac · Electro Hobby 3D"], [t("license"), "GPL-3.0-or-later"]]),
      el("p", { class: "muted mono" }, "github.com/JuanenRac/ARMOR-RADAR")));
}

const HELP_TOPICS = ["overview", "network", "wifi", "broker", "radars", "map", "pins", "users", "update"];
let helpTopic = "overview";
function helpPage() {
  const paragraphs = t("help_" + helpTopic + "_text").split("\n\n");
  return el("div", { class: "help" },
    el("div", { class: "help-nav" }, HELP_TOPICS.map(topic => el("button", { class: topic === helpTopic ? "active" : "", onclick: () => { helpTopic = topic; render(); } }, t("navHelpTab_" + topic)))),
    card(t("help_" + helpTopic + "_title"), ...paragraphs.map(p => el("p", {}, p))));
}

function updatePage() {
  const s = S.status;
  const progress = el("i"), label = el("p", { class: "muted" }), file = el("input", { type: "file", accept: ".bin" });
  const logBox = el("pre", { class: "log", id: "log-box" }, S.log.text);
  const confirmBox = el("input", { placeholder: "RESET" });
  const result = el("p", { class: "muted" });
  return el("div", { class: "grid wide" },
    card(t("updateTitle"), s ? kv([[t("firmware"), s.version], [t("slot"), s.partition]]) : null, el("p", { class: "muted" }, t("updateHelp")),
      el("label", { class: "field" }, el("span", {}, t("chooseFile")), file), el("div", { class: "progress" }, progress), label, result,
      el("div", { class: "actions" }, el("button", { class: "b primary", disabled: !isAdmin(), onclick: () => {
        if (!file.files[0]) return;
        uploadFirmware(file.files[0], progress, label, r => {
          if (r.ok) { result.textContent = t("updateDone", r.version); S.rebooting = true; render(); setTimeout(() => { const wait = async () => { const q = await api("GET", "session"); if (q.ok) location.reload(); else setTimeout(wait, 2000); }; wait(); }, 6000); }
          else { result.textContent = errorText(r.error); result.className = "err"; }
        });
      } }, t("upload")))),
    card(t("githubUpdateTitle"),
      el("div", { class: "actions" }, el("button", { class: "b", tip: "checkGithub", disabled: !isAdmin() || S.github.busy, onclick: githubCheck }, S.github.busy ? t("checking") : t("checkGithub"))),
      S.github.checked && !S.github.error ? el("p", { class: "muted" }, S.github.available ? t("githubAvailable", S.github.latest) : t("githubUpToDate")) : null,
      S.github.error ? el("p", { class: "err" }, S.github.error) : null,
      S.github.available ? el("div", { class: "actions" }, el("button", { class: "b danger", disabled: !isAdmin() || S.github.installing, onclick: githubInstall }, S.github.installing ? t("installing") : t("installUpdate"))) : null,
      note(t("githubUpdateNote"), "info")),
    card(t("maintenance"), el("div", { class: "actions" }, el("button", { class: "b", tip: "rebootNode", disabled: !isAdmin(), onclick: () => confirm(t("confirmAsk")) && reboot() }, t("rebootNode"))),
      el("h3", {}, t("factoryTitle")), el("p", { class: "muted" }, t("factoryHelp")),
      el("div", { class: "actions" }, confirmBox, el("button", { class: "b danger", disabled: !isAdmin(), onclick: async () => {
        const r = await api("POST", "factory-reset", { confirm: confirmBox.value });
        if (r.ok) { S.rebooting = true; render(); setTimeout(() => location.reload(), 6000); } else alert(errorText(r.data.error));
      } }, t("factoryTitle")))),
    card(t("logTitle"), logBox, el("div", { class: "actions" }, el("button", { class: "b", onclick: async () => { S.log = { next: 0, text: "" }; await refreshLog(); } }, t("refresh")))));
}

// ---- data, polling and the shell -------------------------------------------------------------------------------------------------------

async function refreshLive() {
  const [status, pins] = await Promise.all([api("GET", "status"), api("GET", "pins")]);
  if (status.ok) S.status = status.data;
  if (pins.ok) { S.catalog = pins.data.catalog; S.live = pins.data.live; }
}

async function refreshLog() {
  const r = await api("GET", "log?from=" + S.log.next);
  if (!r.ok) return;
  S.log.text = (S.log.text + r.data.text).slice(-24000); S.log.next = r.data.next;
  const box = document.getElementById("log-box");
  if (box) { const bottom = box.scrollTop + box.clientHeight >= box.scrollHeight - 30; box.textContent = S.log.text; if (bottom) box.scrollTop = box.scrollHeight; }
}

let pollTimer = null;
async function poll() {
  if (!S.session || !S.session.authenticated || S.rebooting) return;
  await refreshLive();
  if (S.page === "overview") render(false);
  else if (S.page === "radars" || S.page === "pins") patchLive();
  else if (S.page === "update") await refreshLog();
  updatePills();
}

// The radar and pin pages hold forms: only their live numbers are patched, so what is being typed is not lost.
function patchLive() {
  if (S.page === "radars" && S.status) S.status.radars.forEach((r, i) => {
    const node = document.getElementById("radar-live-" + i);
    if (node && r.enabled) node.textContent = liveText(r);
    const detail = document.getElementById("radar-detail-" + i);
    if (detail && r.enabled) detail.textContent = detailText(r);
  });
}

function updatePills() {
  const box = document.getElementById("pills");
  if (!box || !S.status) return;
  const s = S.status, n = s.network;
  box.replaceChildren(...[
    el("span", { class: "pill " + (n.has_ip ? "ok" : "bad") }, n.has_ip ? n.ip : t("linkDown")),
    s.mqtt.enabled ? el("span", { class: "pill " + (s.mqtt.connected ? "ok" : "warn") }, "MQTT " + (s.mqtt.connected ? t("connected") : t("notConnected"))) : null,
    n.ap_active ? el("span", { class: "pill" }, "AP " + n.ap_clients) : null,
    el("span", { class: "pill" }, "v" + s.version)].filter(Boolean));
}

function shell(content) {
  const groups = [...new Set(PAGES.map(p => p.group))];
  const nav = el("nav", { class: "nav" }, groups.map(g => [el("div", { class: "nav-title" }, t(g)),
    PAGES.filter(p => p.group === g).map(p => el("button", { class: p.id === S.page ? "active" : "", tip: p.label, onclick: () => go(p.id) }, el("span", { class: "ico" }, p.icon), t(p.label)))]));
  const langSelect = el("select", { "aria-label": t("language"), onchange: e => { lang = Number(e.target.value); try { localStorage.setItem("armor_lang", LANGS[lang][0]); } catch (error) { /* ignore */ } document.documentElement.lang = LANGS[lang][0]; persistLanguage(); render(); } },
    LANGS.map((l, i) => el("option", { value: String(i), selected: i === lang }, l[1])));
  const page = PAGES.find(p => p.id === S.page);
  barNode = el("div", { class: "bar", hidden: true });
  const view = el("div", { class: "shell" },
    el("aside", { class: "side" }, el("div", { class: "brand" }, el("div", { class: "brand-mark" }, "A"), el("div", {}, el("strong", {}, "A.R.M.O.R."), el("small", {}, S.session.node_id))), nav,
      el("div", { class: "side-foot" }, el("div", {}, el("span", { class: "dot " + (S.status && S.status.network.has_ip ? "ok" : "bad") }), S.session.user + " · " + (S.session.role === "admin" ? t("roleAdmin") : t("roleViewer"))),
        langSelect, el("button", { class: "b", tip: "signOut", onclick: async () => { await api("POST", "logout", {}); S.session.authenticated = false; start(); } }, t("signOut")))),
    el("main", {}, el("header", { class: "top" }, el("div", {}, el("p", { class: "eyebrow" }, t(page.group)), el("h1", {}, t(page.label))), el("div", { class: "pills", id: "pills" })), content),
    barNode);
  return view;
}

function go(id) {
  if (S.page === "map" && id !== "map") closeMapSocket();
  S.page = id; S.message = S.message && S.message.kind === "ok" ? null : S.message; location.hash = "#/" + id;
}

// A live top-down view of the three radars: the node at the centre, each radar's own cone (coloured, from its calibration above) and
// the targets it currently sees, already placed on the shared plane (and merged where two radars see the same person) by the node
// itself - this page only draws what the WebSocket hands it.
let mapSocket = null, mapTargets = [], mapCanvas = null, mapStatusNode = null, mapLastFrame = 0;
function setMapStatus(key) { if (mapStatusNode) mapStatusNode.textContent = t(key); }
function closeMapSocket() { if (mapSocket) { mapSocket.onclose = null; mapSocket.close(); mapSocket = null; } mapTargets = []; mapCanvas = null; mapStatusNode = null; }
// The page can be built more than once (every render makes a new canvas), so the socket always draws on the canvas that is on screen now.
function openMapSocket() {
  if (mapSocket) return;
  const url = (location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws/radar-map";
  try { mapSocket = new WebSocket(url); } catch (error) { setMapStatus("mapOffline"); return; }
  mapSocket.onopen = () => setMapStatus("mapWaiting");
  mapSocket.onmessage = e => {
    try { mapTargets = (JSON.parse(e.data).targets) || []; } catch (error) { return; }
    mapLastFrame = Date.now(); setMapStatus(mapTargets.length ? "mapLive" : "mapNoTargets");
    if (mapCanvas) drawRadarMap(mapCanvas);
  };
  mapSocket.onclose = () => { mapSocket = null; setMapStatus("mapOffline"); if (S.page === "map") setTimeout(openMapSocket, 1500); };
}

const RADAR_COLORS = ["#e05a5a", "#4caf7d", "#4a8de0"];
const RADAR_RANGE_MM = 6000, RADAR_HALF_FOV = 60;   // the LD2450 sees 120 degrees (plus or minus 60) up to about six metres
function drawRadarMap(canvas) {
  const ctx = canvas.getContext("2d");
  const w = canvas.width, h = canvas.height, cx = w / 2, cy = h / 2, pxPerMm = (w / 2 - 14) / RADAR_RANGE_MM;   // the six-metre ring just fits
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#0b1420"; ctx.fillRect(0, 0, w, h);
  ctx.lineWidth = 1; ctx.font = "10px sans-serif";
  for (let metres = 1; metres <= 6; ++metres) {
    ctx.strokeStyle = metres % 2 ? "#16242f" : "#1f3140"; ctx.beginPath(); ctx.arc(cx, cy, metres * 1000 * pxPerMm, 0, Math.PI * 2); ctx.stroke();
    if (metres % 2 === 0) { ctx.fillStyle = "#4f6b7a"; ctx.fillText(metres + " m", cx + 3, cy - metres * 1000 * pxPerMm - 2); }
  }
  ctx.strokeStyle = "#2a3c4e"; ctx.beginPath(); ctx.moveTo(0, cy); ctx.lineTo(w, cy); ctx.moveTo(cx, 0); ctx.lineTo(cx, h); ctx.stroke();
  (S.cfg ? S.cfg.radars : []).forEach((radar, i) => {
    if (!radar.enabled) return;
    const ox = cx + radar.offset_x_mm * pxPerMm, oy = cy - radar.offset_y_mm * pxPerMm;
    // yaw turns the radar counter-clockwise on the shared plane (the same rotation the node applies to its targets); on a canvas, angles run clockwise
    const heading = (-90 - radar.yaw_deg) * Math.PI / 180, half = RADAR_HALF_FOV * Math.PI / 180, reach = RADAR_RANGE_MM * pxPerMm;
    ctx.fillStyle = RADAR_COLORS[i] + "26";
    ctx.beginPath(); ctx.moveTo(ox, oy); ctx.arc(ox, oy, reach, heading - half, heading + half); ctx.closePath(); ctx.fill();
    ctx.strokeStyle = RADAR_COLORS[i] + "88"; ctx.stroke();
    ctx.fillStyle = RADAR_COLORS[i]; ctx.beginPath(); ctx.arc(ox, oy, 5, 0, Math.PI * 2); ctx.fill();
    ctx.font = "bold 11px sans-serif";
    ctx.fillText((i + 1) + (radar.name ? " " + radar.name : ""), ox + Math.cos(heading) * 22 - 4, oy + Math.sin(heading) * 22 + 4);
  });
  mapTargets.forEach(target => {
    const x = cx + target.x_mm * pxPerMm, y = cy - target.y_mm * pxPerMm;
    ctx.fillStyle = RADAR_COLORS[(target.sensor_id - 1 + 3) % 3] || "#ddd"; ctx.strokeStyle = "#fff"; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.arc(x, y, 7, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    if (Math.abs(target.speed_mm_s) > 50) { ctx.fillStyle = "#fff"; ctx.font = "10px sans-serif"; ctx.fillText((target.speed_mm_s / 1000).toFixed(1) + " m/s", x + 10, y - 8); }
  });
}

// The three radars spread evenly round the node: 360 degrees (120 apart) or the 270 degree layout (75 apart) of the bench guide.
function spreadRadars(yaws) {
  yaws.forEach((yaw, i) => { setValue("radars." + i + ".yaw_deg", yaw); setValue("radars." + i + ".offset_x_mm", 0); setValue("radars." + i + ".offset_y_mm", 0); });
  render();
}

function mapPage() {
  const canvas = el("canvas", { width: 520, height: 520, class: "radar-map" });
  mapCanvas = canvas;
  mapStatusNode = el("p", { class: "muted" }, t(mapSocket ? "mapWaiting" : "mapOffline"));
  openMapSocket();
  drawRadarMap(canvas);
  const same = S.cfg && S.cfg.radars.filter(r => r.enabled).every(r => r.yaw_deg === S.cfg.radars.find(x => x.enabled).yaw_deg);
  return el("div", {}, card(t("mapTitle"), canvas, mapStatusNode,
    el("div", { class: "actions" },
      el("button", { class: "b", tip: "spread360", disabled: !isAdmin(), onclick: () => spreadRadars([0, 120, -120]) }, t("spread360")),
      el("button", { class: "b", tip: "spread270", disabled: !isAdmin(), onclick: () => spreadRadars([-75, 0, 75]) }, t("spread270"))),
    same && S.cfg.radars.filter(r => r.enabled).length > 1 ? note(t("mapSameHeading"), "warn") : null, isDirty() ? note(t("mapUnsaved"), "warn") : null, note(t("mapNote"), "info")));
}

function render(full = true) {
  if (S.rebooting) { $app.replaceChildren(el("div", { class: "center" }, el("div", { class: "login" }, el("h1", {}, t("restarting")), el("p", { class: "muted" }, t("loading"))))); return; }
  if (!S.session || S.session.setup) { $app.replaceChildren(setupScreen()); return; }
  if (!S.session.authenticated) { $app.replaceChildren(loginScreen()); return; }
  if (!S.cfg) { $app.replaceChildren(el("p", { class: "muted" }, t("loading"))); return; }
  const focus = document.activeElement && document.activeElement.tagName === "INPUT" && !full;
  if (focus) return;
  let content;
  switch (S.page) {
    case "network": content = networkPage(); break;
    case "wifi": content = wifiPage(); break;
    case "broker": content = brokerPage(); break;
    case "radars": content = radarsPage(); break;
    case "map": content = mapPage(); break;
    case "pins": content = pinsPage(); break;
    case "users": content = usersPage(); break;
    case "update": content = updatePage(); break;
    case "help": content = helpPage(); break;
    case "about": content = aboutPage(); break;
    default: content = overviewPage();
  }
  $app.replaceChildren(shell(content));
  updatePills();
  refreshBar();
}

// ---- login and set-up screens -----------------------------------------------------------------------------------------------------------

// A password field with an eye to show what was typed - the login and set-up screens are the one place a mistyped password locks
// someone out with no other field to cross-check it against.
function passwordField(labelKey, inputAttrs) {
  const input = el("input", Object.assign({ type: "password" }, inputAttrs));
  const toggle = el("button", { type: "button", class: "eye-toggle", "aria-label": t("showPassword"),
    onclick: () => { input.type = input.type === "password" ? "text" : "password"; toggle.textContent = input.type === "password" ? "👁" : "🙈"; } }, "👁");
  return el("label", { class: "field" }, el("span", {}, t(labelKey)), el("div", { class: "password-row" }, input, toggle));
}

function langPicker() {
  return el("select", { "aria-label": t("language"), onchange: e => { lang = Number(e.target.value); try { localStorage.setItem("armor_lang", LANGS[lang][0]); } catch (error) { /* ignore */ } render(); } },
    LANGS.map((l, i) => el("option", { value: String(i), selected: i === lang }, l[1])));
}

function loginScreen() {
  const form = { user: "", password: "", remember: false };
  const message = el("p", { class: "err" });
  const submit = async e => {
    e.preventDefault();
    const r = await api("POST", "login", form);
    if (r.ok) { await start(); return; }
    message.textContent = r.data.error === "too_many_attempts" && r.data.wait_s ? errorText("too_many_attempts") + " (" + r.data.wait_s + " s)" : errorText(r.data.error);
  };
  return el("div", { class: "center" }, el("form", { class: "login", onsubmit: submit },
    el("div", { class: "brand" }, el("div", { class: "brand-mark" }, "A"), el("div", {}, el("strong", {}, "A.R.M.O.R."), el("small", {}, S.session.node_id))), el("h1", {}, t("signIn")),
    el("label", { class: "field" }, el("span", {}, t("user")), el("input", { autocomplete: "username", autofocus: true, oninput: e => { form.user = e.target.value; } })),
    passwordField("password", { autocomplete: "current-password", oninput: e => { form.password = e.target.value; } }),
    el("label", { class: "check" }, el("input", { type: "checkbox", onchange: e => { form.remember = e.target.checked; } }), t("rememberMe")),
    message, el("button", { class: "b primary", type: "submit" }, t("signIn")), langPicker()));
}

function setupScreen() {
  const form = { code: "", user: "admin", password: "", language: LANGS[lang][0], wifi_ssid: "", wifi_password: "" };
  const message = el("p", { class: "err" });
  const submit = async e => {
    e.preventDefault();
    form.language = LANGS[lang][0];
    const r = await api("POST", "setup", form);
    if (r.ok) { S.rebooting = true; render(); setTimeout(() => { const wait = async () => { const q = await api("GET", "session"); if (q.ok) location.reload(); else setTimeout(wait, 2000); }; wait(); }, 6000); return; }
    message.textContent = errorText(r.data.error);
  };
  return el("div", { class: "center" }, el("form", { class: "login", onsubmit: submit },
    el("div", { class: "brand" }, el("div", { class: "brand-mark" }, "A"), el("div", {}, el("strong", {}, "A.R.M.O.R."), el("small", {}, S.session.node_id))), el("h1", {}, t("setupTitle")),
    el("p", { class: "muted" }, t("setupIntro")), S.session.setup_ssid ? el("p", { class: "hint" }, t("setupWifi") + ": " + S.session.setup_ssid) : null,
    S.session.mac ? el("p", { class: "hint mono" }, t("mac") + ": " + S.session.mac) : null,
    el("label", { class: "field" }, el("span", {}, t("setupCode")), el("input", { autocomplete: "off", autocapitalize: "characters", oninput: e => { form.code = e.target.value.trim().toUpperCase(); } })),
    el("label", { class: "field" }, el("span", {}, t("adminName")), el("input", { value: "admin", autocomplete: "username", oninput: e => { form.user = e.target.value; } })),
    passwordField("newPassword", { autocomplete: "new-password", oninput: e => { form.password = e.target.value; } }),
    ...([
      el("h3", {}, t(hasEthernet() ? "setupWifiOptionalTitle" : "setupWifiTitle")), el("p", { class: "hint" }, t(hasEthernet() ? "setupWifiOptionalHelp" : "setupWifiHelp")),
      el("label", { class: "field" }, el("span", {}, t("ssid")), el("input", { autocomplete: "off", maxLength: 32, oninput: e => { form.wifi_ssid = e.target.value; } })),
      passwordField("wifiPassword", { autocomplete: "off", oninput: e => { form.wifi_password = e.target.value; } })]),
    message, el("button", { class: "b primary", type: "submit" }, t("createAdmin")), langPicker()));
}

// ---- start ---------------------------------------------------------------------------------------------------------------------------------

async function start() {
  const r = await api("GET", "session");
  if (!r.ok) { $app.replaceChildren(el("div", { class: "center" }, el("p", { class: "note bad" }, t("unreachable")))); setTimeout(start, 3000); return; }
  S.session = r.data;
  pickLanguage(S.session.language);
  if (S.session.authenticated) {
    await loadConfig();
    await refreshLive();
    const users = isAdmin() ? await api("GET", "users") : null;
    if (users && users.ok) S.users = users.data;
    S.page = (location.hash.replace("#/", "") || "overview");
    if (!PAGES.some(p => p.id === S.page)) S.page = "overview";
    if (S.page === "update") await refreshLog();
  }
  render();
  clearInterval(pollTimer);
  pollTimer = setInterval(poll, 3000);
}

window.addEventListener("hashchange", () => {
  const id = location.hash.replace("#/", "");
  if (PAGES.some(p => p.id === id) && S.session && S.session.authenticated) { S.page = id; if (id === "update") refreshLog(); render(); }
});
start();
