// ARMOR-RADAR - what a Bluetooth client may ask, and who may ask it: the operations of the configuration channel and their access rules.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The radio, the storage and the network are behind `Backend`, so all of the rules (a node that has no user answers only hello and set-up;
// everything else needs a login; changing anything needs an administrator; wrong passwords are throttled) are tested on a computer.
// The operations are documented in docs/BLE_PROVISIONING.md.
#pragma once
#include <string>
#include <string_view>

#include "auth.hpp"
#include "ble_frame.hpp"
#include "json.hpp"

namespace armor::ble {

class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool has_users() = 0;
  virtual std::string hello_json() = 0;                     // node id, MAC, firmware, whether it is in set-up, the network state
  virtual bool setup_code_ok(std::string_view code) = 0;
  virtual auth::Result add_administrator(std::string_view user, std::string_view password) = 0;
  virtual bool verify(std::string_view user, std::string_view password, auth::Role& role) = 0;
  virtual std::string config_json() = 0;                    // {"config":{...},"channel_auto":n}
  // Applies a (partial) settings document; 0 = saved, otherwise the code; `data` carries {"restart_required":..} or {"problems":[...]}.
  virtual int config_put(std::string_view document, std::string& data) = 0;
  virtual std::string status_json() = 0;
  virtual bool wifi_scan(std::string& data, std::string& error) = 0;
  virtual void restart_soon() = 0;
  virtual std::uint64_t now_ms() = 0;
};

// What one connection has proved.
struct Session {
  bool authenticated = false;
  std::string user;
  auth::Role role = auth::Role::kViewer;
  void clear() { *this = Session{}; }
};

constexpr std::uint32_t kThrottleKey = 0xB1E00001u;   // every Bluetooth client shares one radio, so one throttle

// Answers one request. The reply is always one JSON message (see error_response).
inline std::string handle(Backend& backend, Session& session, auth::LoginThrottle& throttle, const Request& request) {
  const std::uint64_t now = backend.now_ms();
  const auto locked = [&]() { return !throttle.allowed(kThrottleKey, now); };
  const auto lock_reply = [&]() { return error_response(request.id, "too_many_attempts", "{\"wait_s\":" + std::to_string(throttle.wait_s(kThrottleKey, now)) + "}"); };

  if (request.op == "hello") return ok_response(request.id, backend.hello_json());

  if (request.op == "setup") {
    if (backend.has_users()) return error_response(request.id, "forbidden");
    if (locked()) return lock_reply();
    if (!backend.setup_code_ok(request.args.string_or("code", ""))) { throttle.failure(kThrottleKey, now); return error_response(request.id, "wrong_code"); }
    const std::string user = request.args.string_or("user", ""), password = request.args.string_or("password", "");
    const auth::Result result = backend.add_administrator(user, password);
    if (result == auth::Result::kInvalidName) return error_response(request.id, "invalid_name");
    if (result == auth::Result::kWeakPassword) return error_response(request.id, "weak_password");
    if (result != auth::Result::kOk) return error_response(request.id, "storage");
    throttle.success(kThrottleKey);
    session.authenticated = true;
    session.user = user;
    session.role = auth::Role::kAdmin;
    // No restart yet: the app is signed in as the administrator and goes on with the configuration on this same connection, then sends `reboot`.
    return ok_response(request.id, "{\"restart_required\":true}");
  }

  if (backend.has_users() == false) return error_response(request.id, "setup_required");

  if (request.op == "login") {
    if (locked()) return lock_reply();
    auth::Role role = auth::Role::kViewer;
    const std::string user = request.args.string_or("user", "");
    if (!backend.verify(user, request.args.string_or("password", ""), role)) { throttle.failure(kThrottleKey, now); session.clear(); return error_response(request.id, "wrong_credentials"); }
    throttle.success(kThrottleKey);
    session.authenticated = true;
    session.user = user;
    session.role = role;
    return ok_response(request.id, std::string("{\"role\":\"") + auth::to_text(role) + "\"}");
  }
  if (request.op == "logout") { session.clear(); return ok_response(request.id); }

  if (!session.authenticated) return error_response(request.id, "unauthorized");
  const bool admin = session.role == auth::Role::kAdmin;

  if (request.op == "config.get") return ok_response(request.id, backend.config_json());
  if (request.op == "status") return ok_response(request.id, backend.status_json());
  if (request.op == "config.put") {
    if (!admin) return error_response(request.id, "forbidden");
    const json::Value* config = request.args.get("config");
    if (config == nullptr || !config->is_object()) return error_response(request.id, "invalid");
    // The document is sent again as text: the backend parses and checks it the same way as the panel's request.
    json::Writer w;
    // (the request's own text is not kept, so rebuild the object from the parsed value)
    std::string text;
    struct Rebuild {
      static void write(json::Writer& out, const json::Value& value) {
        switch (value.type) {
          case json::Type::kNull: out.null(); break;
          case json::Type::kBool: out.boolean(value.boolean); break;
          case json::Type::kNumber: out.number(value.number); break;
          case json::Type::kString: out.string(value.text); break;
          case json::Type::kArray: out.begin_array(); for (const json::Value& item : value.items) write(out, item); out.end_array(); break;
          case json::Type::kObject: out.begin_object(); for (std::size_t i = 0; i < value.items.size(); ++i) { out.key(value.names[i]); write(out, value.items[i]); } out.end_object(); break;
        }
      }
    };
    Rebuild::write(w, *config);
    std::string data;
    const int code = backend.config_put(w.str(), data);
    if (code == 0) return ok_response(request.id, data.empty() ? "{}" : data);
    return error_response(request.id, code == 422 ? "invalid" : "storage", data);
  }
  if (request.op == "wifi.scan") {
    if (!admin) return error_response(request.id, "forbidden");
    std::string data, error;
    if (!backend.wifi_scan(data, error)) return error_response(request.id, error.empty() ? "scan_failed" : error);
    return ok_response(request.id, data);
  }
  if (request.op == "reboot") {
    if (!admin) return error_response(request.id, "forbidden");
    backend.restart_soon();
    return ok_response(request.id);
  }
  return error_response(request.id, "unknown_op");
}

}  // namespace armor::ble
