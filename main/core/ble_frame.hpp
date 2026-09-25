// ARMOR-RADAR - the framing of the Bluetooth configuration channel: JSON messages carried over a GATT byte stream.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A GATT write or notification carries at most (MTU - 3) bytes, often 20, while a configuration is a few kilobytes of JSON. The channel is
// therefore a byte stream in each direction, and a message is [length: 2 bytes, big-endian][that many bytes of UTF-8 JSON]. The receiver
// collects bytes until the length is reached; the sender cuts a message into chunks of any size. Nothing here touches the radio.
//
// A request is {"id":7,"op":"config.get"} or {"id":8,"op":"login","args":{...}}; the answer is {"id":7,"ok":true,"data":{...}} or
// {"id":7,"ok":false,"error":"code"}, and the id is echoed so an app can match them. The operations are in docs/BLE_PROVISIONING.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace armor::ble {

constexpr std::size_t kMaxMessage = 6000;   // the largest message either way (the settings document is about 3 KB)
constexpr std::size_t kHeader = 2;

// The bytes of one message: its length and its JSON. Empty when the message is too long or empty.
inline std::string frame(std::string_view json_text) {
  if (json_text.empty() || json_text.size() > kMaxMessage) return "";
  std::string out;
  out += static_cast<char>((json_text.size() >> 8) & 0xFF);
  out += static_cast<char>(json_text.size() & 0xFF);
  out.append(json_text);
  return out;
}

// The pieces a message is sent in: chunks of at most `chunk` bytes (an ATT payload), in order.
inline std::vector<std::string> chunks_of(const std::string& framed, std::size_t chunk) {
  std::vector<std::string> out;
  if (chunk == 0) return out;
  for (std::size_t at = 0; at < framed.size(); at += chunk) out.push_back(framed.substr(at, chunk));
  return out;
}

// Collects the bytes written by the app and hands over whole messages. A length of zero or above kMaxMessage is a protocol error: the
// assembler drops what it has and starts again from the next byte (the app resends).
class Assembler {
 public:
  // Feeds bytes; returns true, with the message in `message`, when one is complete. Bytes after a complete message stay for the next call.
  bool feed(const std::uint8_t* data, std::size_t length, std::string& message) {
    buffer_.append(reinterpret_cast<const char*>(data), length);
    return take(message);
  }
  // A message already complete inside what was fed before (an app may write two messages back to back).
  bool take(std::string& message) {
    if (buffer_.size() < kHeader) return false;
    const std::size_t wanted = (static_cast<std::size_t>(static_cast<unsigned char>(buffer_[0])) << 8) | static_cast<unsigned char>(buffer_[1]);
    if (wanted == 0 || wanted > kMaxMessage) { buffer_.clear(); ++errors_; return false; }
    if (buffer_.size() < kHeader + wanted) return false;
    message = buffer_.substr(kHeader, wanted);
    buffer_.erase(0, kHeader + wanted);
    return true;
  }
  void reset() { buffer_.clear(); }
  std::size_t pending() const { return buffer_.size(); }
  unsigned errors() const { return errors_; }

 private:
  std::string buffer_;
  unsigned errors_ = 0;
};

struct Request {
  long long id = 0;
  std::string op;
  json::Value args;   // an object, empty when absent
};

// Reads a request; false when it is not one (no object, no id or no operation).
inline bool parse_request(std::string_view text, Request& out) {
  json::Value document;
  if (!json::parse(text, document) || !document.is_object()) return false;
  const json::Value* id = document.get("id");
  const json::Value* op = document.get("op");
  if (id == nullptr || !id->is_number() || id->number < 0 || id->number != static_cast<double>(static_cast<long long>(id->number)) || op == nullptr || !op->is_string() || op->text.empty() || op->text.size() > 32) return false;
  out.id = static_cast<long long>(id->number);
  out.op = op->text;
  const json::Value* args = document.get("args");
  out.args = args != nullptr && args->is_object() ? *args : json::Value{};
  if (out.args.type == json::Type::kNull) out.args.type = json::Type::kObject;
  return true;
}

// {"id":7,"ok":true,"data":<data_json>}; data_json is already JSON ("{}" when there is nothing to say).
inline std::string ok_response(long long id, std::string_view data_json = "{}") {
  json::Writer w;
  w.begin_object().key("id").integer(id).field("ok", true).key("data").raw(data_json.empty() ? "{}" : data_json).end_object();
  return w.str();
}
inline std::string error_response(long long id, std::string_view code, std::string_view detail_json = "") {
  json::Writer w;
  w.begin_object().key("id").integer(id).field("ok", false).field("error", code);
  if (!detail_json.empty()) w.key("data").raw(detail_json);
  w.end_object();
  return w.str();
}

}  // namespace armor::ble
