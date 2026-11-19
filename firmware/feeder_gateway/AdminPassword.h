#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// One atomic NVS record changes both the verifier and the browser-session epoch.
class AdminPassword {
 public:
  static const size_t kRecordSize = 49; // version, 16-byte salt, 32-byte PBKDF2 verifier
  typedef bool (*Derive)(const char *, const uint8_t *, uint8_t *);
  typedef void (*Random)(uint8_t *, size_t);
  typedef bool (*Write)(const uint8_t *, size_t);
  enum Result { Changed, WrongCurrent, InvalidNew, Mismatch, StorageFailure };
  AdminPassword(const char *bootstrap, Derive derive, Random random)
      : bootstrap_(bootstrap), derive_(derive), random_(random) {}
  void load(const uint8_t *record, size_t length, bool exists) {
    saved_ = exists;
    valid_ = exists && length == kRecordSize && record && record[0] == 1;
    if (valid_) memcpy(record_, record, kRecordSize);
  }
  bool matches(const char *password) const {
    if (!password || strlen(password) > 512) return false;
    if (!saved_) return bootstrap_[0] && strcmp(bootstrap_, "CHANGE-ME") && equal(password, bootstrap_);
    if (!valid_) return false; // Corrupt persisted credentials never restore the bootstrap password.
    uint8_t verifier[32] = {};
    if (!derive_(password, record_ + 1, verifier)) return false;
    unsigned difference = 0;
    for (size_t i = 0; i < 32; ++i) difference |= verifier[i] ^ record_[17 + i];
    return difference == 0;
  }
  Result change(const char *current, const char *next, const char *confirmation, Write write) {
    if (!matches(current)) return WrongCurrent;
    const size_t length = characters(next);
    if (length < 12 || length > 128 || strlen(next) > 512) return InvalidNew;
    if (!equal(next, confirmation)) return Mismatch;
    uint8_t candidate[kRecordSize] = {1};
    random_(candidate + 1, 16);
    if (!derive_(next, candidate + 1, candidate + 17) || !write(candidate, sizeof(candidate))) return StorageFailure;
    load(candidate, sizeof(candidate), true);
    return Changed;
  }
  // Persist this tag with each session. A password change invalidates every old tag,
  // including across resets, without depending on four separate session-key deletes.
  void sessionTag(char out[33]) const {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
      const uint8_t b = valid_ ? record_[1 + i] : 0;
      out[i * 2] = hex[b >> 4]; out[i * 2 + 1] = hex[b & 15];
    }
    out[32] = 0;
  }
  bool acceptsSession(const char *persisted, const char *token) const {
    if (!sessionEpochValid() || strlen(token) != 64) return false;
    if (!saved_ && equal(persisted, token)) return true;
    if (strlen(persisted) != 97 || persisted[32] != '.') return false;
    char tag[33]; sessionTag(tag);
    return strncmp(persisted, tag, 32) == 0 && equal(persisted + 33, token);
  }
  bool sessionEpochValid() const { return !saved_ || valid_; }
  bool acceptsLegacySessions() const { return !saved_; }
 private:
  static size_t characters(const char *text) {
    size_t count = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
      if ((*p & 0xc0) != 0x80) ++count;
    return count;
  }
  static bool equal(const char *a, const char *b) {
    const size_t length = strlen(a);
    if (length != strlen(b)) return false;
    unsigned difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return difference == 0;
  }
  const char *bootstrap_;
  Derive derive_;
  Random random_;
  bool saved_ = false, valid_ = false;
  uint8_t record_[kRecordSize] = {};
};
