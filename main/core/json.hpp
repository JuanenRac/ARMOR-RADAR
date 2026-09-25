// ARMOR-RADAR - a small, strict JSON reader and writer for the node's settings and its web API.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The panel and the node speak JSON, and the settings are stored as JSON, so the parser sees text that comes from the network: it is
// strict (no comments, no trailing commas, no trailing text), bounded (depth and size) and never throws. It is small on purpose and
// is tested on a computer together with the rest of the core (tests/test_core.cpp).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace armor::json {

enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

struct Value {
  Type type = Type::kNull;
  bool boolean = false;
  double number = 0.0;
  std::string text;
  std::vector<Value> items;        // array items, or the values of an object
  std::vector<std::string> names;  // the names of an object, parallel to `items`

  bool is_object() const { return type == Type::kObject; }
  bool is_array() const { return type == Type::kArray; }
  bool is_string() const { return type == Type::kString; }
  bool is_number() const { return type == Type::kNumber; }
  bool is_bool() const { return type == Type::kBool; }

  // The member of an object, or nullptr (also when this is not an object). The first of a repeated name wins.
  const Value* get(std::string_view name) const {
    if (type != Type::kObject) return nullptr;
    for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == name) return &items[i];
    return nullptr;
  }
  std::string string_or(std::string_view name, std::string fallback) const {
    const Value* member = get(name);
    return member != nullptr && member->is_string() ? member->text : fallback;
  }
  double number_or(std::string_view name, double fallback) const {
    const Value* member = get(name);
    return member != nullptr && member->is_number() ? member->number : fallback;
  }
  bool bool_or(std::string_view name, bool fallback) const {
    const Value* member = get(name);
    return member != nullptr && member->is_bool() ? member->boolean : fallback;
  }
  // A whole number in a range, or the fallback when it is missing, not a number, fractional or out of range.
  long long integer_or(std::string_view name, long long fallback, long long lowest, long long highest) const {
    const Value* member = get(name);
    if (member == nullptr || !member->is_number() || !std::isfinite(member->number) || member->number != std::floor(member->number)) return fallback;
    if (member->number < static_cast<double>(lowest) || member->number > static_cast<double>(highest)) return fallback;
    return static_cast<long long>(member->number);
  }
};

constexpr std::size_t kMaxDepth = 12;
constexpr std::size_t kMaxText = 64 * 1024;

namespace detail {
class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  bool parse(Value& out) {
    if (text_.size() > kMaxText) return false;
    skip_space();
    if (!value(out, 0)) return false;
    skip_space();
    return at_ == text_.size();
  }

 private:
  std::string_view text_;
  std::size_t at_ = 0;

  bool more() const { return at_ < text_.size(); }
  char peek() const { return text_[at_]; }
  void skip_space() { while (more() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) ++at_; }
  bool literal(std::string_view word) {
    if (text_.substr(at_, word.size()) != word) return false;
    at_ += word.size();
    return true;
  }

  bool value(Value& out, std::size_t depth) {
    if (depth > kMaxDepth || !more()) return false;
    switch (peek()) {
      case '{': return object(out, depth);
      case '[': return array(out, depth);
      case '"': out.type = Type::kString; return string(out.text);
      case 't': out.type = Type::kBool; out.boolean = true; return literal("true");
      case 'f': out.type = Type::kBool; out.boolean = false; return literal("false");
      case 'n': out.type = Type::kNull; return literal("null");
      default: return number(out);
    }
  }

  bool number(Value& out) {
    const std::size_t start = at_;
    if (more() && peek() == '-') ++at_;
    if (!more()) return false;
    if (peek() == '0') ++at_;
    else if (peek() >= '1' && peek() <= '9') while (more() && peek() >= '0' && peek() <= '9') ++at_;
    else return false;
    if (more() && peek() == '.') {
      ++at_;
      const std::size_t digits = at_;
      while (more() && peek() >= '0' && peek() <= '9') ++at_;
      if (at_ == digits) return false;
    }
    if (more() && (peek() == 'e' || peek() == 'E')) {
      ++at_;
      if (more() && (peek() == '+' || peek() == '-')) ++at_;
      const std::size_t digits = at_;
      while (more() && peek() >= '0' && peek() <= '9') ++at_;
      if (at_ == digits) return false;
    }
    const std::string token(text_.substr(start, at_ - start));
    out.type = Type::kNumber;
    out.number = std::strtod(token.c_str(), nullptr);
    return std::isfinite(out.number);
  }

  static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  bool four_hex(unsigned& value) {
    if (at_ + 4 > text_.size()) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const int digit = hex_digit(text_[at_ + static_cast<std::size_t>(i)]);
      if (digit < 0) return false;
      value = value * 16 + static_cast<unsigned>(digit);
    }
    at_ += 4;
    return true;
  }
  static void append_utf8(std::string& out, unsigned code) {
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else if (code < 0x10000) { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
  }

  bool string(std::string& out) {
    out.clear();
    ++at_;  // the opening quote
    while (more()) {
      const unsigned char c = static_cast<unsigned char>(peek());
      ++at_;
      if (c == '"') return true;
      if (c < 0x20) return false;  // a raw control character is not allowed in a JSON string
      if (c != '\\') { out += static_cast<char>(c); continue; }
      if (!more()) return false;
      const char escape = peek();
      ++at_;
      switch (escape) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned code = 0;
          if (!four_hex(code)) return false;
          if (code >= 0xD800 && code <= 0xDBFF) {  // a surrogate pair carries one character beyond the first plane
            unsigned low = 0;
            if (at_ + 2 > text_.size() || text_[at_] != '\\' || text_[at_ + 1] != 'u') return false;
            at_ += 2;
            if (!four_hex(low) || low < 0xDC00 || low > 0xDFFF) return false;
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          } else if (code >= 0xDC00 && code <= 0xDFFF) {
            return false;
          }
          append_utf8(out, code);
          break;
        }
        default: return false;
      }
    }
    return false;  // the text ended inside the string
  }

  bool array(Value& out, std::size_t depth) {
    out.type = Type::kArray;
    ++at_;
    skip_space();
    if (more() && peek() == ']') { ++at_; return true; }
    for (;;) {
      Value item;
      skip_space();
      if (!value(item, depth + 1)) return false;
      out.items.push_back(std::move(item));
      skip_space();
      if (!more()) return false;
      if (peek() == ',') { ++at_; continue; }
      if (peek() == ']') { ++at_; return true; }
      return false;
    }
  }

  bool object(Value& out, std::size_t depth) {
    out.type = Type::kObject;
    ++at_;
    skip_space();
    if (more() && peek() == '}') { ++at_; return true; }
    for (;;) {
      skip_space();
      if (!more() || peek() != '"') return false;
      std::string name;
      if (!string(name)) return false;
      skip_space();
      if (!more() || peek() != ':') return false;
      ++at_;
      skip_space();
      Value member;
      if (!value(member, depth + 1)) return false;
      out.names.push_back(std::move(name));
      out.items.push_back(std::move(member));
      skip_space();
      if (!more()) return false;
      if (peek() == ',') { ++at_; continue; }
      if (peek() == '}') { ++at_; return true; }
      return false;
    }
  }
};
}  // namespace detail

// Parses a whole document. False (and `out` unspecified) when it is not valid JSON.
inline bool parse(std::string_view text, Value& out) {
  out = Value{};
  return detail::Parser(text).parse(out);
}

// A JSON string literal, quotes included, with every character that needs it escaped.
inline std::string quote(std::string_view text) {
  std::string out = "\"";
  for (const char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { char code[8]; std::snprintf(code, sizeof code, "\\u%04x", c); out += code; }
        else out += raw;
    }
  }
  out += '"';
  return out;
}

// Builds a document in order, placing the commas itself: begin_object(); key("a"); number(1); end_object().
class Writer {
 public:
  Writer& begin_object() { separate(); out_ += '{'; first_ = true; return *this; }
  Writer& end_object() { out_ += '}'; first_ = false; return *this; }
  Writer& begin_array() { separate(); out_ += '['; first_ = true; return *this; }
  Writer& end_array() { out_ += ']'; first_ = false; return *this; }
  Writer& key(std::string_view name) { separate(); out_ += quote(name); out_ += ':'; after_key_ = true; return *this; }
  Writer& string(std::string_view text) { separate(); out_ += quote(text); return *this; }
  Writer& boolean(bool value) { separate(); out_ += value ? "true" : "false"; return *this; }
  Writer& null() { separate(); out_ += "null"; return *this; }
  Writer& integer(long long value) { separate(); out_ += std::to_string(value); return *this; }
  // `decimals` < 0: the shortest faithful text (up to nine significant digits), otherwise a fixed number of decimals.
  Writer& number(double value, int decimals = -1) {
    separate();
    if (!std::isfinite(value)) { out_ += "null"; return *this; }
    char text[40];
    if (decimals < 0) std::snprintf(text, sizeof text, "%.9g", value);
    else std::snprintf(text, sizeof text, "%.*f", decimals, value);
    out_ += text;
    return *this;
  }
  // Shorthands for a member: field("name", value).
  Writer& field(std::string_view name, std::string_view text) { return key(name).string(text); }
  Writer& field(std::string_view name, const char* text) { return key(name).string(text); }
  Writer& field(std::string_view name, bool value) { return key(name).boolean(value); }
  Writer& field(std::string_view name, int value) { return key(name).integer(value); }
  Writer& field(std::string_view name, long long value) { return key(name).integer(value); }
  Writer& field(std::string_view name, unsigned value) { return key(name).integer(value); }
  Writer& field(std::string_view name, double value) { return key(name).number(value); }
  Writer& raw(std::string_view already_json) { separate(); out_ += already_json; return *this; }

  const std::string& str() const { return out_; }

 private:
  void separate() {
    if (after_key_) { after_key_ = false; return; }
    if (!first_) out_ += ',';
    first_ = false;
  }
  std::string out_;
  bool first_ = true;
  bool after_key_ = false;
};

}  // namespace armor::json
