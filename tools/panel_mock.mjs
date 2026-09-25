#!/usr/bin/env node
/**
 * ARMOR-RADAR - a stand-in for a node, to work on the web panel without a board.
 * Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
 *
 *   node tools/panel_mock.mjs [port]          (default 8090)     open http://127.0.0.1:8090/
 *
 * It serves the panel from panel/ (text.js is joined in front of app.js, as tools/pack_panel.py does) and answers /api/v1 like the firmware
 * does, from memory: a fresh mock is in set-up (code TESTCODE); `--user admin:adminpass123` starts it with an administrator. It is a development
 * tool: it checks far less than the firmware, and its numbers are made up.
 */
import { createServer } from "node:http";
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const panel = path.join(here, "..", "panel");
const port = Number(process.argv.find(a => /^\d+$/.test(a)) ?? 8090);
const seeded = process.argv.includes("--user") ? process.argv[process.argv.indexOf("--user") + 1] : "";

const users = new Map();
if (seeded) { const [name, password] = seeded.split(":"); users.set(name, { password, role: "admin" }); }
const sessions = new Map();
const started = Date.now();
let rebootAt = 0;
let logText = "I (1200) armor-node: A.R.M.O.R. node armor-a1b2c3, firmware 0.2.3\nI (1500) armor-net: link up\nI (2600) armor-net: address 192.168.0.181, gateway 192.168.0.1, netmask 255.255.255.0 (wire)\nW (9000) armor-radar: radar 2: no data (RX not connected, no power or the wrong pin) (0 bytes, 0 frames, 0 bad)\n";

const config = {
  v: 1, node: { id: "armor-a1b2c3", name: "Perimeter 1" }, uplink: "ethernet",
  ip: { dhcp: true, address: "", netmask: "255.255.255.0", gateway: "", dns1: "", dns2: "", hostname: "" },
  ap: { enabled: true, ssid: "ARMOR", security: "wpa2", password_set: true, channel: 0, hidden: false, max_clients: 8, tx_power_dbm: 15, bandwidth_mhz: 20, country: "ES", bridge: true },
  sta: { enabled: false, ssid: "", password_set: false },
  mqtt: { enabled: true, uri: "mqtt://192.168.0.180:18883", username: "field-node-a1b2c3", password_set: true, heartbeat_s: 10, telemetry_ms: 200, ntp: "pool.ntp.org" },
  radars: [{ enabled: true, rx: 16, tx: 15 }, { enabled: true, rx: 17, tx: 21 }, { enabled: false, rx: 18, tx: 38 }],
  sensors: { veml7700: true, sda: 1, scl: 2, lux_fallback: -1 },
  pins: [
    { gpio: 39, name: "garden_light", mode: "output", invert: true, pull: "none", initial_on: false, safe: "off", link_timeout_s: 60, pulse_ms: 800, debounce_ms: 30, period_s: 10, freq_hz: 1000, report: "on", scale: 1, offset: 0 },
    { gpio: 40, name: "gate_contact", mode: "input", invert: false, pull: "up", initial_on: false, safe: "keep", link_timeout_s: 0, pulse_ms: 0, debounce_ms: 30, period_s: 10, freq_hz: 1000, report: "open", scale: 1, offset: 0 },
    { gpio: 2, name: "battery", mode: "adc", invert: false, pull: "none", initial_on: false, safe: "keep", link_timeout_s: 0, pulse_ms: 0, debounce_ms: 30, period_s: 10, freq_hz: 1000, report: "battery", scale: 0.0057, offset: 0 },
  ],
  ui: { language: "en" },
};
const live = { garden_light: { on: false, percent: 0, value: 0, has_value: false }, gate_contact: { on: true, percent: 0, value: 0, has_value: false }, battery: { on: false, percent: 0, value: 12.61, has_value: true } };

const catalog = [];
for (let gpio = 0; gpio <= 48; ++gpio) {
  if (gpio >= 22 && gpio <= 25) continue;
  let use = "free", reason = "", header = true;
  if (gpio >= 26 && gpio <= 32) { use = "reserved"; reason = "flash"; header = false; }
  else if (gpio >= 33 && gpio <= 37) { use = "reserved"; reason = "psram"; }
  else if (gpio === 19 || gpio === 20) { use = "reserved"; reason = "usb"; }
  else if (gpio >= 9 && gpio <= 14) { use = "reserved"; reason = "ethernet"; header = false; }
  else if (gpio === 8) { use = "reserved"; reason = "camera"; header = false; }
  else if (gpio >= 4 && gpio <= 7) use = "sd";
  else if ([0, 3, 45, 46].includes(gpio)) use = "caution";
  catalog.push({ gpio, use, reason, on_header: header, adc1: gpio >= 1 && gpio <= 10 });
}

const json = (response, code, body, headers = {}) => { response.writeHead(code, { "Content-Type": "application/json", "Cache-Control": "no-store", ...headers }); response.end(JSON.stringify(body)); };
const bodyOf = request => new Promise(resolve => { const chunks = []; request.on("data", c => chunks.push(c)); request.on("end", () => resolve(Buffer.concat(chunks))); });
const tokenOf = request => /armor_session=([0-9a-f]+)/.exec(request.headers.cookie ?? "")?.[1] ?? "";

function status() {
  return {
    node_id: config.node.id, name: config.node.name, version: "0.2.3", uptime_s: Math.floor((Date.now() - started) / 1000) + 5400, reset_reason: "power_on", heap_free: 182000, heap_min: 151000, psram_free: 7400000, partition: "ota_0",
    network: { layout: config.ap.enabled ? "ethernet+ap-bridged" : "ethernet", link_up: true, has_ip: true, ip: "192.168.0.181", netmask: "255.255.255.0", gateway: "192.168.0.1", dns: "192.168.0.1", mac: "34:85:18:a1:b2:c3", ethernet_ok: true,
      ap_active: config.ap.enabled, ap_setup: users.size === 0, ap_bridged: config.ap.bridge, ap_ssid: users.size === 0 ? "ARMOR-SETUP-A1B2C3" : config.ap.ssid, ap_channel: 6, ap_clients: 2, sta_connected: false, sta_ssid: "", sta_rssi: 0 },
    mqtt: { enabled: config.mqtt.enabled, connected: true, clock_set: true, published: 4120 + Math.floor((Date.now() - started) / 200), withheld: "" },
    lux: 312.4,
    radars: config.radars.map((r, i) => ({ radar: i + 1, enabled: r.enabled, rx: r.rx, tx: r.tx, state: !r.enabled ? "disabled" : i === 1 ? "no_data" : "reporting", bytes: i === 1 ? 0 : 812345, frames: i === 1 ? 0 : 27020, bad_frames: 3, fps: i === 1 ? 0 : 9.9, firmware: i === 0 ? "1.02.22062416" : "", tracking_mode: i === 0 ? 2 : 0 })),
  };
}

const problems = doc => {
  const list = [];
  if (doc.ap?.enabled && !String(doc.ap.ssid ?? "").trim()) list.push({ path: "ap.ssid", code: "required" });
  if (doc.ap?.enabled && doc.ap.security !== "open" && doc.ap.password !== undefined && doc.ap.password.length > 0 && doc.ap.password.length < 8) list.push({ path: "ap.password", code: "invalid_key" });
  if (doc.mqtt?.enabled && !/^mqtts?:\/\/.+/.test(doc.mqtt.uri ?? "")) list.push({ path: "mqtt.uri", code: "invalid" });
  return list;
};

const server = createServer(async (request, response) => {
  const url = new URL(request.url, "http://x");
  if (request.method === "GET" && ["/", "/index.html"].includes(url.pathname)) { response.writeHead(200, { "Content-Type": "text/html" }); return response.end(readFileSync(path.join(panel, "index.html"))); }
  if (request.method === "GET" && url.pathname === "/style.css") { response.writeHead(200, { "Content-Type": "text/css" }); return response.end(readFileSync(path.join(panel, "style.css"))); }
  if (request.method === "GET" && url.pathname === "/app.js") { response.writeHead(200, { "Content-Type": "text/javascript" }); return response.end(readFileSync(path.join(panel, "text.js"), "utf8") + "\n" + readFileSync(path.join(panel, "app.js"), "utf8")); }
  if (!url.pathname.startsWith("/api/v1/")) return json(response, 404, { error: "not_found" });

  const route = url.pathname.slice(8), method = request.method;
  const raw = await bodyOf(request);
  let body = {};
  if (raw.length && !url.pathname.endsWith("/ota")) { try { body = JSON.parse(raw.toString("utf8")); } catch { return json(response, 400, { error: "not_json" }); } }
  const session = sessions.get(tokenOf(request));
  const setup = users.size === 0;

  if (method === "GET" && route === "session") return json(response, 200, { setup, authenticated: !!session, user: session?.user ?? "", role: session?.role ?? "", node_id: config.node.id, language: config.ui.language, version: "0.2.3", setup_ssid: setup ? "ARMOR-SETUP-A1B2C3" : "", mac: "34:85:18:a1:b2:c3" });
  if (method === "POST" && route === "setup") {
    if (!setup) return json(response, 403, { error: "forbidden" });
    if (body.code !== "TESTCODE") return json(response, 403, { error: "wrong_code" });
    if (!/^[a-z0-9_.-]{3,32}$/.test(body.user ?? "")) return json(response, 422, { error: "invalid_name" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    users.set(body.user, { password: body.password, role: "admin" });
    return json(response, 200, { ok: true, restart_required: true });
  }
  if (method === "POST" && route === "login") {
    const user = users.get(body.user);
    if (!user || user.password !== body.password) return json(response, 401, { error: "wrong_credentials" });
    const token = [...Array(48)].map(() => "0123456789abcdef"[Math.floor(Math.random() * 16)]).join("");
    sessions.set(token, { user: body.user, role: user.role });
    return json(response, 200, { ok: true, restart_required: false }, { "Set-Cookie": `armor_session=${token}; Path=/; HttpOnly; SameSite=Strict` });
  }
  if (method === "POST" && route === "logout") { sessions.delete(tokenOf(request)); return json(response, 200, { ok: true }); }
  if (setup) return json(response, 403, { error: "setup_required" });
  if (!session) return json(response, 401, { error: "unauthorized" });
  if (method !== "GET" && request.headers["x-requested-with"] !== "armor") return json(response, 403, { error: "forbidden" });
  const admin = session.role === "admin";
  const needAdmin = () => { if (!admin) { json(response, 403, { error: "forbidden" }); return false; } return true; };

  if (method === "GET" && route === "status") return json(response, 200, status());
  if (method === "GET" && route === "config") return json(response, 200, { config, channel_auto: 6, firmware: "0.2.3" });
  if (method === "PUT" && route === "config") {
    if (!needAdmin()) return;
    const list = problems(body);
    if (list.length) return json(response, 422, { error: "invalid", problems: list });
    for (const key of Object.keys(body)) {
      const value = body[key];
      if (value && typeof value === "object" && !Array.isArray(value) && config[key] && typeof config[key] === "object") Object.assign(config[key], value);
      else config[key] = value;
    }
    for (const section of [config.ap, config.sta, config.mqtt]) if (typeof section.password === "string" && section.password) { section.password_set = true; delete section.password; } else delete section.password;
    return json(response, 200, { ok: true, restart_required: true });
  }
  if (method === "GET" && route === "pins") return json(response, 200, { catalog, live: config.pins.filter(p => p.mode !== "disabled").map(p => ({ name: p.name, gpio: p.gpio, mode: p.mode, report: p.report, ...(live[p.name] ?? { on: false, percent: 0, value: 0, has_value: false }), fell_back: false, topic_state: `armor/device/${config.node.id}/${p.name}/state`, topic_set: "" })) });
  if (method === "POST" && route === "pins/command") {
    if (!needAdmin()) return;
    const pin = live[body.name];
    if (!pin) return json(response, 404, { error: "unknown_pin" });
    if (body.command === "on") pin.on = true; else if (body.command === "off") pin.on = false; else if (body.command === "toggle") pin.on = !pin.on; else if (body.command === "pulse") { pin.on = true; setTimeout(() => { pin.on = false; }, 800); } else return json(response, 422, { error: "invalid_command" });
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "GET" && route === "radars") return json(response, 200, status().radars);
  if (method === "POST" && route === "radars/command") {
    if (!needAdmin()) return;
    const zones = { type: 1, list: [{ x1: -500, y1: 300, x2: 500, y2: 2500 }, { x1: 0, y1: 0, x2: 0, y2: 0 }, { x1: 0, y1: 0, x2: 0, y2: 0 }] };
    if (body.radar === 2) return json(response, 200, { ok: false, error: "no_answer", firmware: "", tracking_mode: 0, last_answer: "" });
    return json(response, 200, { ok: true, error: "", firmware: body.op === "read_info" ? "1.02.22062416" : "", tracking_mode: body.op === "single" ? 1 : 2, last_answer: "0000010040", ...(body.op === "read_info" ? { zones } : {}) });
  }
  if (method === "GET" && route === "users") { if (!needAdmin()) return; return json(response, 200, [...users].map(([name, u]) => ({ name, role: u.role }))); }
  if (method === "POST" && route === "users") {
    if (!needAdmin()) return;
    if (!/^[a-z0-9_.-]{3,32}$/.test(body.name ?? "")) return json(response, 422, { error: "invalid_name" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    if (users.has(body.name)) return json(response, 409, { error: "exists" });
    users.set(body.name, { password: body.password, role: body.role === "admin" ? "admin" : "viewer" });
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "PUT" && route.startsWith("users/")) {
    if (!needAdmin()) return;
    const user = users.get(route.slice(6));
    if (!user) return json(response, 404, { error: "not_found" });
    if (body.role) user.role = body.role;
    if (body.password) user.password = body.password;
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "DELETE" && route.startsWith("users/")) {
    if (!needAdmin()) return;
    const name = route.slice(6);
    if (users.get(name)?.role === "admin" && [...users.values()].filter(u => u.role === "admin").length === 1) return json(response, 409, { error: "last_admin" });
    users.delete(name);
    return json(response, users.has(name) ? 500 : 200, { ok: true, restart_required: false });
  }
  if (method === "PUT" && route === "account") {
    const user = users.get(session.user);
    if (user.password !== body.current) return json(response, 403, { error: "wrong_password" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    user.password = body.password;
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "GET" && route === "log") return json(response, 200, { next: logText.length, text: logText.slice(Number(url.searchParams.get("from") ?? 0)) });
  if (method === "POST" && route === "reboot") { if (!needAdmin()) return; rebootAt = Date.now(); return json(response, 200, { ok: true, restart_required: false }); }
  if (method === "POST" && route === "factory-reset") { if (!needAdmin()) return; if (body.confirm !== "RESET") return json(response, 422, { error: "confirm_required" }); users.clear(); return json(response, 200, { ok: true, restart_required: true }); }
  if (method === "POST" && route === "ota") { if (!needAdmin()) return; return json(response, 200, { ok: true, restart_required: true, version: "0.2.4", bytes: raw.length, sha256: "0".repeat(64) }); }
  return json(response, 404, { error: "not_found" });
});
server.listen(port, "127.0.0.1", () => console.log(`ARMOR node panel mock on http://127.0.0.1:${port}/ (${users.size ? "signed-in users: " + [...users.keys()].join(", ") : "set-up code TESTCODE"})`));
