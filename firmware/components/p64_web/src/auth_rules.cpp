#include "auth_rules.hpp"

namespace p64::web::auth::rules {

bool valid_pin(const std::string &pin) {
  if (pin.size() < 4 || pin.size() > 8) return false;
  for (char c : pin) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

int Lockout::locked_seconds(int64_t now_us) const {
  return locked_until_us_ > now_us ? static_cast<int>((locked_until_us_ - now_us + 999999) / 1000000) : 0;
}

bool Lockout::attempt(bool right, int64_t now_us, bool *just_locked) {
  if (just_locked) *just_locked = false;
  if (locked_seconds(now_us) > 0) return false;
  if (right) {
    failures_ = 0;
    return true;
  }
  if (++failures_ >= kMaxFailures) {
    failures_ = 0;
    locked_until_us_ = now_us + kLockoutUs;
    if (just_locked) *just_locked = true;
  }
  return false;
}

Sessions::Slot *Sessions::find(const char *token) {
  if (!token || !token[0]) return nullptr;
  for (Slot &s : slots_) {
    if (s.token[0] && std::strcmp(s.token, token) == 0) return &s;
  }
  return nullptr;
}

const char *Sessions::create(const uint8_t random[16], int64_t now_us) {
  Slot *slot = nullptr;
  for (Slot &s : slots_) {
    if (!s.token[0]) {
      slot = &s;
      break;
    }
    if (!slot || s.last_seen_us < slot->last_seen_us) slot = &s;  // the least recently seen
  }
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 16; ++i) {
    slot->token[i * 2] = hex[random[i] >> 4];
    slot->token[i * 2 + 1] = hex[random[i] & 15];
  }
  slot->token[32] = 0;
  slot->last_seen_us = now_us;
  return slot->token;
}

bool Sessions::touch(const char *token, int64_t now_us) {
  Slot *s = find(token);
  if (!s) return false;
  s->last_seen_us = now_us;
  return true;
}

void Sessions::end(const char *token) {
  if (Slot *s = find(token)) s->token[0] = 0;
}

void Sessions::end_all() {
  for (Slot &s : slots_) s.token[0] = 0;
}

int Sessions::count() const {
  int n = 0;
  for (const Slot &s : slots_) n += s.token[0] ? 1 : 0;
  return n;
}

}  // namespace p64::web::auth::rules
