#pragma once
// Caller serializes access. One active command and one replaceable pending state.
struct LatestCommand {
  bool pending=false, active=false, target=false;
  int result=0; // none, queued, sending, confirmed, failed
  void request(bool open) { target=open; pending=true; result=1; }
  bool take(bool &open) {
    if(active || !pending) return false;
    open=target; pending=false; active=true; result=2; return true;
  }
  void finish(bool ok) { active=false; result=pending?1:(ok?3:4); }
  void disconnect() { if(pending || active) result=4; pending=false; active=false; }
  bool busy() const { return active || pending; }
};
