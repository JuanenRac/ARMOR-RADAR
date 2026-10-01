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
    return el("label", { class: "check" }, input, t(labelKey), problem ? el("span", { class: "err" }, problemText(problem)) : null);
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
  return el("label", { class: "field" }, el("span", {}, t(labelKey)), input, problem ? el("span", { class: "err" }, problemText(problem)) : null, options.hint ? el("span", { class: "hint" }, options.hint) : null);
}

// ---- the save bar --------------------------------------------------------------------------------------------------------------------

let barNode = null;
function refreshBar() {
  if (!barNode) return;
  const dirty = isDirty();
  const message = S.message || (dirty ? { kind: "warn", text: t("unsaved") } : S.restartNeeded ? { kind: "warn", text: t("savedRestart") } : null);
  barNode.replaceChildren(...[
    el("span", { class: "msg " + (message ? message.kind : "") }, message ? message.text : ""),
    S.restartNeeded && !dirty ? el("button", { class: "b danger", onclick: () => reboot() }, t("restart")) : null,
    dirty ? el("button", { class: "b", onclick: discard }, t("discard")) : null,
    dirty ? el("button", { class: "b primary", disabled: S.busy, onclick: () => save(false) }, t("save")) : null,
    dirty ? el("button", { class: "b danger", disabled: S.busy, onclick: () => save(true) }, t("saveRestart")) : null].filter(Boolean));
  barNode.hidden = !(message || dirty || S.restartNeeded);
}

function discard() { S.cfg = JSON.parse(S.saved); S.problems = {}; S.message = null; render(); }

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
  { id: "pins", icon: "▦", label: "navPins", group: "navSensors" },
  { id: "users", icon: "☺", label: "navUsers", group: "navSystem" },
  { id: "update", icon: "⟳", label: "navUpdate", group: "navSystem" },
];

const card = (title, ...kids) => el("section", { class: "card" }, title ? el("h2", {}, title) : null, ...kids);
const kv = rows => el("dl", { class: "kv" }, rows.filter(Boolean).flatMap(([k, v]) => [el("dt", {}, k), el("dd", {}, v)]));
const note = (text, kind) => el("p", { class: "note " + (kind || "") }, text);

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
    card(t("ovNetwork"), kv([[t("board"), BOARD_NAMES[n.board] || n.board || "—"], [t("layout"), n.layout], [t("link"), n.link_up ? t("linkUp") : t("linkDown")], [t("address"), n.has_ip ? n.ip : "—"], [t("netmask"), n.netmask || "—"], [t("gateway"), n.gateway || "—"],
      [t("dns"), n.dns || "—"], [t("mac"), n.mac],
      n.ap_active ? [t("apActive"), "“" + n.ap_ssid + "” · " + t("channel") + " " + n.ap_channel + " · " + n.ap_clients + " " + t("apClients") + " · " + layoutNote] : null,
      n.sta_ssid ? [t("station"), n.sta_ssid + (n.sta_connected ? " · " + n.sta_rssi + " dBm" : " · " + t("notConnected"))] : null]),
      n.ethernet_available === false || n.ethernet_ok ? null : note(t("ethernetMissing"), "bad")),
    card(t("ovBroker"), kv([[t("navBroker"), !m.enabled ? t("notConfigured") : m.connected ? t("connected") : t("notConnected")], [t("clock"), m.clock_set ? t("clockSet") : t("clockNotSet")], [t("messages"), m.published]]),
      m.withheld === "light" ? note(t("withheldLight")) : m.withheld === "radars" ? note(t("withheldRadars")) : null),
    card(t("radarsTitle"), el("div", { class: "row" }, s.radars.map(r => el("div", {}, el("h3", {}, t("sensorN", r.radar), r.enabled && r.model ? " · " + modelOf(r.model).label : ""), statePill(r.enabled ? r.state : "disabled"),
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
    card(t("bleTitle"), field("bleMode", "ble.mode", { type: "select", options: [["setup", t("bleSetup")], ["always", t("bleAlways")], ["off", t("bleOff")]] }), note(t("bleNote"), "info")));
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
      el("div", { class: "actions" }, el("button", { class: "b", disabled: !isAdmin() || S.scan.busy, onclick: scanNetworks }, t("scanNetworks"))), scanResults(),
      el("p", { class: "hint" }, t("scanNote")), note(t("staNote"), "info")));
}

function brokerPage() {
  const cfg = S.cfg, mq = cfg.mqtt;
  return el("div", { class: "grid wide" },
    card(t("brokerTitle"), field("brokerEnable", "mqtt.enabled", { type: "checkbox", rerender: true }),
      mq.enabled ? [field("brokerUri", "mqtt.uri", { placeholder: "mqtt://192.168.0.180:18883" }),
        el("div", { class: "row" }, field("brokerUser", "mqtt.username"), field("brokerPassword", "mqtt.password", { type: "password" })),
        el("div", { class: "row" }, field("heartbeat", "mqtt.heartbeat_s", { type: "number", min: 2, max: 300 }), field("telemetryMs", "mqtt.telemetry_ms", { type: "number", min: 200, max: 5000, step: 100 }), field("ntp", "mqtt.ntp"))] : null,
      note(t("brokerNote"), "info")),
    card(t("sensorsTitle"), field("veml", "sensors.veml7700", { type: "checkbox", rerender: true }),
      cfg.sensors.veml7700 ? el("div", { class: "row" }, field("sda", "sensors.sda", { type: "number", min: 0, max: 48 }), field("scl", "sensors.scl", { type: "number", min: 0, max: 48 })) : null,
      field("luxFallback", "sensors.lux_fallback", { type: "number", min: -1, max: 200000 }), note(t("luxNote"))));
}

// ---- pins and radars ------------------------------------------------------------------------------------------------------------------

function pinLabel(info) {
  return "GPIO " + info.gpio + (info.use === "caution" ? " ⚠" : info.use === "sd" ? " (microSD)" : "") + (info.on_header ? "" : " *");
}
function pinOptions(selected, includeNone) {
  const assignable = S.catalog.filter(p => p.use !== "reserved");
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
    el("div", { class: "row" }, field("pinGpio", base + "gpio", { type: "select", number: true, options: pinOptions(pin.gpio, false) }), field("pinName", base + "name", { max: 24 }),
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
    isAdmin() && pins.length < 16 ? el("div", { class: "actions" }, el("button", { class: "b primary", onclick: addPin }, t("addPin"))) : null);
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
  await refreshLive(); render();
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
  const zone = S.zones[index] || (S.zones[index] = { type: 0, list: [{ x1: 0, y1: 0, x2: 0, y2: 0 }, { x1: 0, y1: 0, x2: 0, y2: 0 }, { x1: 0, y1: 0, x2: 0, y2: 0 }] });
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
      model.tracker ? null : el("div", { class: "row" }, field("deviceName", base + "name", { max: 24, hint: t("deviceNameHint") })),
      el("div", { class: "row" }, field("rxPin", base + "rx", { type: "select", number: true, options: pinOptions(cfg.rx, false) }), field("txPin", base + "tx", { type: "select", number: true, options: pinOptions(cfg.tx, true) }))] : null,
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
  return el("div", {}, note(t("radarWarn")), el("div", { class: "grid wide" }, [0, 1, 2].map(radarCard)));
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
          if (r.ok) { result.textContent = t("updateDone", r.version); S.rebooting = true; setTimeout(() => { const wait = async () => { const q = await api("GET", "session"); if (q.ok) location.reload(); else setTimeout(wait, 2000); }; wait(); }, 6000); }
          else { result.textContent = errorText(r.error); result.className = "err"; }
        });
      } }, t("upload")))),
    card(t("maintenance"), el("div", { class: "actions" }, el("button", { class: "b", disabled: !isAdmin(), onclick: () => confirm(t("confirmAsk")) && reboot() }, t("rebootNode"))),
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
    PAGES.filter(p => p.group === g).map(p => el("button", { class: p.id === S.page ? "active" : "", onclick: () => go(p.id) }, el("span", { class: "ico" }, p.icon), t(p.label)))]));
  const langSelect = el("select", { "aria-label": t("language"), onchange: e => { lang = Number(e.target.value); try { localStorage.setItem("armor_lang", LANGS[lang][0]); } catch (error) { /* ignore */ } document.documentElement.lang = LANGS[lang][0]; render(); } },
    LANGS.map((l, i) => el("option", { value: String(i), selected: i === lang }, l[1])));
  const page = PAGES.find(p => p.id === S.page);
  barNode = el("div", { class: "bar", hidden: true });
  const view = el("div", { class: "shell" },
    el("aside", { class: "side" }, el("div", { class: "brand" }, el("div", { class: "brand-mark" }, "A"), el("div", {}, el("strong", {}, "A.R.M.O.R."), el("small", {}, S.session.node_id))), nav,
      el("div", { class: "side-foot" }, el("div", {}, el("span", { class: "dot " + (S.status && S.status.network.has_ip ? "ok" : "bad") }), S.session.user + " · " + (S.session.role === "admin" ? t("roleAdmin") : t("roleViewer"))),
        langSelect, el("button", { class: "b", onclick: async () => { await api("POST", "logout", {}); S.session.authenticated = false; start(); } }, t("signOut")))),
    el("main", {}, el("header", { class: "top" }, el("div", {}, el("p", { class: "eyebrow" }, t(page.group)), el("h1", {}, t(page.label))), el("div", { class: "pills", id: "pills" })), content),
    barNode);
  return view;
}

function go(id) { S.page = id; S.message = S.message && S.message.kind === "ok" ? null : S.message; location.hash = "#/" + id; }

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
    case "pins": content = pinsPage(); break;
    case "users": content = usersPage(); break;
    case "update": content = updatePage(); break;
    default: content = overviewPage();
  }
  $app.replaceChildren(shell(content));
  updatePills();
  refreshBar();
}

// ---- login and set-up screens -----------------------------------------------------------------------------------------------------------

function langPicker() {
  return el("select", { "aria-label": t("language"), onchange: e => { lang = Number(e.target.value); try { localStorage.setItem("armor_lang", LANGS[lang][0]); } catch (error) { /* ignore */ } render(); } },
    LANGS.map((l, i) => el("option", { value: String(i), selected: i === lang }, l[1])));
}

function loginScreen() {
  const form = { user: "", password: "" };
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
    el("label", { class: "field" }, el("span", {}, t("password")), el("input", { type: "password", autocomplete: "current-password", oninput: e => { form.password = e.target.value; } })),
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
    el("label", { class: "field" }, el("span", {}, t("newPassword")), el("input", { type: "password", autocomplete: "new-password", oninput: e => { form.password = e.target.value; } })),
    ...(hasEthernet() ? [] : [
      el("h3", {}, t("setupWifiTitle")), el("p", { class: "hint" }, t("setupWifiHelp")),
      el("label", { class: "field" }, el("span", {}, t("ssid")), el("input", { autocomplete: "off", maxLength: 32, oninput: e => { form.wifi_ssid = e.target.value; } })),
      el("label", { class: "field" }, el("span", {}, t("wifiPassword")), el("input", { type: "password", autocomplete: "off", oninput: e => { form.wifi_password = e.target.value; } }))]),
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
