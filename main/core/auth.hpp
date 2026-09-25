// ARMOR-RADAR - who may use the panel: users, roles, sessions and the throttle on wrong passwords.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Passwords are stored only as a salted hash. The hash function itself (PBKDF2 with HMAC-SHA256 on the board, a stand-in in the tests)
// is passed in, so all the rules around it - who exists, who is admin, when a login is refused, when a session ends - are tested on a
// computer. A node that has no user yet is in "setup": the first administrator is created with a code that is only shown on the node's
// own console, so nobody on the network can claim a fresh node.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace armor::auth {

enum class Role { kViewer, kAdmin };
constexpr std::size_t kMaxUsers = 4;
constexpr std::size_t kMinPassword = 8, kMaxPassword = 64;

inline const char* to_text(Role role) { return role == Role::kAdmin ? "admin" : "viewer"; }

struct User {
  std::string name;
  Role role = Role::kViewer;
  std::string salt;  // hexadecimal
  std::string hash;  // hexadecimal
};

// Computes the stored hash of a password for a salt (both text); the same inputs always give the same output.
using Hasher = std::function<std::string(std::string_view password, std::string_view salt)>;

inline bool valid_user_name(std::string_view name) {
  if (name.size() < 3 || name.size() > 32) return false;
  for (const char c : name) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return false;
  return true;
}
inline bool valid_password(std::string_view password) {
  if (password.size() < kMinPassword || password.size() > kMaxPassword) return false;
  for (const char c : password) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
  return true;
}

// Compares two texts in a time that does not depend on where they first differ.
inline bool same_text(std::string_view a, std::string_view b) {
  unsigned char difference = static_cast<unsigned char>(a.size() != b.size());
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) difference = static_cast<unsigned char>(difference | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
  return difference == 0;
}

// Text made from random bytes (the caller supplies them from the hardware generator).
inline std::string to_hex(const std::uint8_t* bytes, std::size_t length) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < length; ++i) { out += digits[bytes[i] >> 4]; out += digits[bytes[i] & 15]; }
  return out;
}
// The setup code: capital letters and digits without the ones that look alike (0 O 1 I L). 31 symbols, so the byte is reduced with a small, harmless bias.
inline std::string setup_code_from(const std::uint8_t* bytes, std::size_t length) {
  static const char alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
  std::string out;
  for (std::size_t i = 0; i < length; ++i) out += alphabet[bytes[i] % 31];
  return out;
}

enum class Result { kOk, kInvalidName, kWeakPassword, kExists, kNotFound, kTooMany, kLastAdmin, kWrongPassword };

class UserStore {
 public:
  bool empty() const { return users_.empty(); }
  const std::vector<User>& users() const { return users_; }
  const User* find(std::string_view name) const {
    for (const User& user : users_) if (user.name == name) return &user;
    return nullptr;
  }
  std::size_t admins() const { return static_cast<std::size_t>(std::count_if(users_.begin(), users_.end(), [](const User& u) { return u.role == Role::kAdmin; })); }

  Result add(std::string_view name, std::string_view password, Role role, std::string_view salt, const Hasher& hasher) {
    if (!valid_user_name(name)) return Result::kInvalidName;
    if (!valid_password(password)) return Result::kWeakPassword;
    if (find(name) != nullptr) return Result::kExists;
    if (users_.size() >= kMaxUsers) return Result::kTooMany;
    users_.push_back({std::string(name), role, std::string(salt), hasher(password, salt)});
    return Result::kOk;
  }
  // The role of the user when the password is right.
  bool verify(std::string_view name, std::string_view password, const Hasher& hasher, Role& role) const {
    const User* user = find(name);
    // An unknown name still costs one hash, so the answer time does not say which names exist.
    const std::string computed = hasher(password, user != nullptr ? std::string_view(user->salt) : std::string_view("00000000000000000000000000000000"));
    if (user == nullptr || !same_text(computed, user->hash)) return false;
    role = user->role;
    return true;
  }
  Result set_password(std::string_view name, std::string_view password, std::string_view salt, const Hasher& hasher) {
    User* user = find_mutable(name);
    if (user == nullptr) return Result::kNotFound;
    if (!valid_password(password)) return Result::kWeakPassword;
    user->salt = std::string(salt);
    user->hash = hasher(password, salt);
    return Result::kOk;
  }
  Result set_role(std::string_view name, Role role) {
    User* user = find_mutable(name);
    if (user == nullptr) return Result::kNotFound;
    if (user->role == Role::kAdmin && role != Role::kAdmin && admins() == 1) return Result::kLastAdmin;
    user->role = role;
    return Result::kOk;
  }
  Result remove(std::string_view name) {
    const User* user = find(name);
    if (user == nullptr) return Result::kNotFound;
    if (user->role == Role::kAdmin && admins() == 1) return Result::kLastAdmin;
    users_.erase(std::remove_if(users_.begin(), users_.end(), [&](const User& u) { return u.name == name; }), users_.end());
    return Result::kOk;
  }
  void clear() { users_.clear(); }

  // Storage: {"users":[{"name":"admin","role":"admin","salt":"..","hash":".."}]}
  std::string to_json() const {
    json::Writer w;
    w.begin_object().key("users").begin_array();
    for (const User& user : users_) w.begin_object().field("name", user.name).field("role", to_text(user.role)).field("salt", user.salt).field("hash", user.hash).end_object();
    w.end_array().end_object();
    return w.str();
  }
  bool from_json(std::string_view text) {
    json::Value document;
    if (!json::parse(text, document)) return false;
    const json::Value* list = document.get("users");
    if (list == nullptr || !list->is_array() || list->items.size() > kMaxUsers) return false;
    std::vector<User> loaded;
    for (const json::Value& item : list->items) {
      User user;
      user.name = item.string_or("name", "");
      const std::string role = item.string_or("role", "");
      user.salt = item.string_or("salt", "");
      user.hash = item.string_or("hash", "");
      if (!valid_user_name(user.name) || (role != "admin" && role != "viewer") || user.salt.empty() || user.hash.empty() || find_in(loaded, user.name)) return false;
      user.role = role == "admin" ? Role::kAdmin : Role::kViewer;
      loaded.push_back(std::move(user));
    }
    users_ = std::move(loaded);
    return true;
  }

 private:
  static bool find_in(const std::vector<User>& list, std::string_view name) {
    return std::any_of(list.begin(), list.end(), [&](const User& u) { return u.name == name; });
  }
  User* find_mutable(std::string_view name) {
    for (User& user : users_) if (user.name == name) return &user;
    return nullptr;
  }
  std::vector<User> users_;
};

// ---- sessions ------------------------------------------------------------------------------------------------------------------

constexpr std::size_t kMaxSessions = 6;
constexpr std::uint64_t kSessionIdleMs = 30ULL * 60ULL * 1000ULL;

struct Session {
  std::string token;  // random, hexadecimal; created by the caller
  std::string user;
  Role role = Role::kViewer;
  std::uint64_t last_seen_ms = 0;
  bool used = false;
};

class SessionTable {
 public:
  // Starts a session; when the table is full the one idle the longest is replaced.
  void create(std::string token, std::string user, Role role, std::uint64_t now_ms) {
    Session* slot = nullptr;
    for (Session& session : sessions_) if (!session.used || now_ms - session.last_seen_ms > kSessionIdleMs) { slot = &session; break; }
    if (slot == nullptr) slot = &*std::min_element(sessions_.begin(), sessions_.end(), [](const Session& a, const Session& b) { return a.last_seen_ms < b.last_seen_ms; });
    *slot = {std::move(token), std::move(user), role, now_ms, true};
  }
  // The session for a token, refreshed; nullptr when there is none or it has been idle too long.
  const Session* touch(std::string_view token, std::uint64_t now_ms) {
    if (token.size() < 32) return nullptr;
    for (Session& session : sessions_) {
      if (!session.used || !same_text(session.token, token)) continue;
      if (now_ms - session.last_seen_ms > kSessionIdleMs) { session = Session{}; return nullptr; }
      session.last_seen_ms = now_ms;
      return &session;
    }
    return nullptr;
  }
  void end(std::string_view token) {
    for (Session& session : sessions_) if (session.used && same_text(session.token, token)) session = Session{};
  }
  // Ends every session of a user (after a password change or a removal).
  void end_user(std::string_view user) {
    for (Session& session : sessions_) if (session.used && session.user == user) session = Session{};
  }
  void clear() { for (Session& session : sessions_) session = Session{}; }

 private:
  std::array<Session, kMaxSessions> sessions_{};
};

// ---- the throttle --------------------------------------------------------------------------------------------------------------

// After five wrong passwords from one address the address is refused for 30 seconds, then for twice as long each time (at most five
// minutes); a right password forgives. The table holds the eight most recent addresses.
class LoginThrottle {
 public:
  bool allowed(std::uint32_t source, std::uint64_t now_ms) const {
    const Entry* entry = find(source);
    return entry == nullptr || now_ms >= entry->blocked_until_ms;
  }
  // Seconds to wait, for the message (0 when allowed).
  unsigned wait_s(std::uint32_t source, std::uint64_t now_ms) const {
    const Entry* entry = find(source);
    if (entry == nullptr || now_ms >= entry->blocked_until_ms) return 0;
    return static_cast<unsigned>((entry->blocked_until_ms - now_ms + 999) / 1000);
  }
  void failure(std::uint32_t source, std::uint64_t now_ms) {
    Entry& entry = obtain(source, now_ms);
    entry.last_ms = now_ms;
    if (++entry.failures >= 5) {
      const unsigned doublings = std::min(entry.failures - 5, 4u);
      entry.blocked_until_ms = now_ms + std::min<std::uint64_t>(30000ULL << doublings, 300000ULL);
    }
  }
  void success(std::uint32_t source) {
    for (Entry& entry : entries_) if (entry.used && entry.source == source) entry = Entry{};
  }

 private:
  struct Entry {
    bool used = false;
    std::uint32_t source = 0;
    unsigned failures = 0;
    std::uint64_t blocked_until_ms = 0, last_ms = 0;
  };
  const Entry* find(std::uint32_t source) const {
    for (const Entry& entry : entries_) if (entry.used && entry.source == source) return &entry;
    return nullptr;
  }
  Entry& obtain(std::uint32_t source, std::uint64_t now_ms) {
    for (Entry& entry : entries_) if (entry.used && entry.source == source) return entry;
    Entry* slot = nullptr;
    for (Entry& entry : entries_) if (!entry.used) { slot = &entry; break; }
    if (slot == nullptr) slot = &*std::min_element(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.last_ms < b.last_ms; });
    *slot = Entry{};
    slot->used = true;
    slot->source = source;
    slot->last_ms = now_ms;
    return *slot;
  }
  std::array<Entry, 8> entries_{};
};

}  // namespace armor::auth
