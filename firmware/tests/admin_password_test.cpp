#include <assert.h>
#include <vector>
#include <string>
#include "../feeder_gateway/AdminPassword.h"

std::vector<uint8_t> stored;
bool saveOk = true, deriveOk = true;
unsigned nonce = 0;
void randomBytes(uint8_t *out, size_t length) { for(size_t i=0;i<length;++i) out[i]=++nonce; }
// Deliberately cheap deterministic test transport. Production uses SDK PBKDF2.
bool derive(const char *password, const uint8_t *salt, uint8_t *out) {
  if(!deriveOk) return false;
  for(size_t i=0;i<32;++i) out[i]=salt[i%16];
  for(size_t i=0;i<strlen(password);++i) out[i%32]^=password[i];
  return true;
}
bool save(const uint8_t *data, size_t length) {
  if(!saveOk) return false;
  stored.assign(data,data+length); return true;
}
AdminPassword makePassword() { return AdminPassword("initial-password",derive,randomBytes); }
int main() {
  auto password=makePassword();
  password.load(nullptr,0,false);
  assert(password.matches("initial-password"));
  assert(!password.matches("wrong-password"));
  assert(password.acceptsLegacySessions());
  char oldTag[33]; password.sessionTag(oldTag);
  const std::string token(64, 'a');
  const std::string oldSession=std::string(oldTag)+"."+token;
  assert(password.acceptsSession(token.c_str(),token.c_str()));
  assert(password.acceptsSession(oldSession.c_str(),token.c_str()));
  assert(password.change("wrong-password","replacement-password","replacement-password",save)==AdminPassword::WrongCurrent);
  assert(password.change("initial-password","short","short",save)==AdminPassword::InvalidNew);
  assert(password.change("initial-password","replacement-password","different-password",save)==AdminPassword::Mismatch);
  const char tooLong[130] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  assert(password.change("initial-password",tooLong,tooLong,save)==AdminPassword::InvalidNew);
  assert(stored.empty());
  char longPassword[130]; memset(longPassword,'x',129);longPassword[129]=0;
  assert(password.change("initial-password",longPassword,longPassword,save)==AdminPassword::InvalidNew);
  saveOk=false;
  assert(password.change("initial-password","replacement-password","replacement-password",save)==AdminPassword::StorageFailure);
  assert(password.matches("initial-password"));
  assert(password.acceptsSession(oldSession.c_str(),token.c_str()));
  saveOk=true;deriveOk=false;
  assert(password.change("initial-password","replacement-password","replacement-password",save)==AdminPassword::StorageFailure);
  deriveOk=true;
  assert(password.change("initial-password","replacement-password","replacement-password",save)==AdminPassword::Changed);
  assert(!password.matches("initial-password"));
  assert(password.matches("replacement-password"));
  assert(!password.acceptsLegacySessions());
  char newTag[33];password.sessionTag(newTag);assert(strcmp(oldTag,newTag));
  const std::string newSession=std::string(newTag)+"."+token;
  assert(!password.acceptsSession(token.c_str(),token.c_str()));
  assert(!password.acceptsSession(oldSession.c_str(),token.c_str()));
  assert(password.acceptsSession(newSession.c_str(),token.c_str()));
  // Reset/OTA loads persisted credentials, ignoring even a changed build default.
  AdminPassword rebooted("different-build-default",derive,randomBytes);
  rebooted.load(stored.data(),stored.size(),true);
  assert(rebooted.matches("replacement-password"));
  assert(rebooted.acceptsSession(newSession.c_str(),token.c_str()));
  assert(!rebooted.acceptsSession(oldSession.c_str(),token.c_str()));
  assert(!rebooted.matches("different-build-default"));
  char rebootTag[33];rebooted.sessionTag(rebootTag);assert(!strcmp(newTag,rebootTag));
  assert(rebooted.change("replacement-password","another-password","another-password",save)==AdminPassword::Changed);
  rebooted.sessionTag(rebootTag);assert(strcmp(newTag,rebootTag));
  assert(!rebooted.acceptsSession(newSession.c_str(),token.c_str()));
  // Bad persisted data fails closed instead of restoring the built-in password.
  for(size_t length: {size_t(0),size_t(2),AdminPassword::kRecordSize}) {
    uint8_t corrupt[AdminPassword::kRecordSize]={7};
    rebooted.load(corrupt,length,true);
    assert(!rebooted.matches("different-build-default"));
    assert(!rebooted.acceptsLegacySessions());
    assert(!rebooted.sessionEpochValid());
    assert(!rebooted.acceptsSession(oldSession.c_str(),token.c_str()));
    assert(!rebooted.acceptsSession(newSession.c_str(),token.c_str()));
    assert(!rebooted.sessionEpochValid());
    assert(!rebooted.acceptsSession(oldSession.c_str(),token.c_str()));
    assert(!rebooted.acceptsSession(newSession.c_str(),token.c_str()));
  }
  auto unicode=makePassword();
  assert(unicode.change("initial-password",u8"éééééé",u8"éééééé",save)==AdminPassword::InvalidNew);
  assert(unicode.change("initial-password",u8"éééééééééééé",u8"éééééééééééé",save)==AdminPassword::Changed);
  assert(unicode.matches(u8"éééééééééééé"));
  auto unset=AdminPassword("CHANGE-ME",derive,randomBytes);
  assert(!unset.matches("CHANGE-ME"));
  return 0;
}
