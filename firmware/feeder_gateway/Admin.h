#pragma once
#include <Update.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_ota_ops.h>
#include "admin_credentials.h"

const char kFirmwareVersion[] = "1.0.0-feeder";
uint32_t rebootAt = 0;
String recentLogs[32];
unsigned logNext = 0, logCount = 0;

void logEvent(const String &message) {
  String line = String(millis()) + " ms: " + message;
  Serial.println(line);
  remoteStates.event(message);
  recentLogs[logNext] = line;
  logNext = (logNext + 1) % 32;
  if (logCount < 32) ++logCount;
}

// Persistent, independently revocable browser sessions. No idle timeout.
Preferences sessionStore;
String adminSessions[4];
bool sessionStoreReady = false;
unsigned nextSession = 0;
const char loginPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Feeder login</title>
<style>body{font:16px system-ui;max-width:420px;margin:60px auto;padding:20px}input,button{font:inherit;padding:10px;box-sizing:border-box;width:100%;margin:8px 0}label{display:block}</style>
<h1>Chicken feeder login</h1><form method="POST" action="/login">
<label>Username<input name="username" autocomplete="username" required></label>
<label>Password<input name="password" type="password" autocomplete="current-password" required></label>
<button>Log in</button></form></html>)HTML";

String sessionCookie() {
  String cookies = server.header("Cookie");
  int start = 0;
  while (start < static_cast<int>(cookies.length())) {
    int end = cookies.indexOf(';', start);
    if (end < 0) end = cookies.length();
    String part = cookies.substring(start, end); part.trim();
    if (part.startsWith("feeder_session=")) return part.substring(15);
    start = end + 1;
  }
  return "";
}

int sessionIndex() {
  String token = sessionCookie();
  if (token.length() != 64) return -1;
  for (int i = 0; i < 4; ++i) if (adminSessions[i] == token) return i;
  return -1;
}

bool adminAuthenticated() {
  // Deliberately ignore cached HTTP Basic credentials: they must not undo logout.
  return sessionStoreReady && sessionIndex() >= 0;
}

bool requireAdmin() {
  server.sendHeader("Cache-Control", "no-store");
  if (adminAuthenticated()) return true;
  if (server.method() == HTTP_GET) server.send_P(401, "text/html", loginPage);
  else server.send(401, "text/plain", "Please log in again at /login");
  return false;
}

bool sameOrigin() {
  return server.header("Origin") == String("http://") + server.hostHeader();
}

String diagnostics() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  String result = "Firmware: " + String(kFirmwareVersion) + "\nBuilt: " + __DATE__ + " " + __TIME__;
  result += "\nIP: " + ETH.localIP().toString() + "\nEthernet MAC: " + ETH.macAddress();
  result += "\nLink: " + String(ETH.linkUp() ? "up" : "down");
  result += "\nFree heap: " + String(ESP.getFreeHeap()) + " bytes";
  result += "\nFree PSRAM: " + String(ESP.getFreePsram()) + " bytes";
  result += "\nRunning partition: " + String(running ? running->label : "unknown");
  result += "\nNext OTA partition: " + String(next ? next->label : "unavailable");
  result += temperatureDiagnostics();
  result += "\n\n" + statusJson() + "\n";
  return result;
}

const char adminPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Chicken feeder console</title>
<style>body{font:16px system-ui;max-width:850px;margin:40px auto;padding:0 20px}pre{background:#f1f3f4;padding:18px;white-space:pre-wrap;overflow-wrap:anywhere}button{font:inherit;padding:9px;cursor:pointer}button[aria-pressed="true"]{background:#1670d2;color:white;border-color:#1670d2}section{margin:24px 0}details{border-top:1px solid #ddd;margin-top:32px;padding-top:18px}summary{font-size:1.3rem;cursor:pointer}.command-row{display:flex;gap:8px;margin:8px 0}.command-row button{flex:1;min-width:0}</style>
<form method="POST" action="/logout" style="float:right"><button>Log Out</button></form><h1>GO feeder controls</h1><p>Blue buttons show the current state. Command results appear below.</p>
<section id="console"><h2>Feeder</h2>
<div class="command-row"><button type="button" data-command="open" aria-pressed="false">Open</button><button type="button" data-command="close" aria-pressed="false">Close</button></div>
<p id="feeder-state">State: unknown</p></section>
<details id="management"><summary>Management</summary>
<div class="command-row"><button type="button" data-command="status">Status</button><button type="button" data-command="logs">Log</button><button type="button" data-command="config">Configuration</button><button type="button" data-command="help">Help</button></div>
<section id="bluetooth-config">
<h2>Feeder Bluetooth link</h2>
<div class="command-row"><button type="button" data-command="feeder connect" aria-pressed="false">Connect</button><button type="button" data-command="feeder disconnect" aria-pressed="false">Disconnect</button></div>
<p id="feeder-link-state"></p><p>Release your phone connection before reconnecting the feeder here.</p>
<p>This preference survives gateway restarts.</p>
</section>
<div class="command-row"><button type="button" data-command="reboot">Reboot gateway</button></div>
<p id="firmware-link"><a href="/update">Upload firmware</a></p></details>
<pre id="output" aria-live="polite">Connecting…</pre>
<script>
const output=document.getElementById('output');
function pressed(command,active){document.querySelector('[data-command="'+command+'"]').setAttribute('aria-pressed',String(active));}
function updateLink(state){
  pressed('open',state.physical_state==='open');
  pressed('close',state.physical_state==='closed');
  document.getElementById('feeder-state').textContent='State: '+(state.physical_state||'unknown')+' · Command: '+(state.last_result||'unknown');
  document.querySelector('[data-command="feeder connect"]').setAttribute('aria-pressed',String(state.feeder_connection_enabled===true));
  document.querySelector('[data-command="feeder disconnect"]').setAttribute('aria-pressed',String(state.feeder_connection_enabled===false));
  document.getElementById('feeder-link-state').textContent=state.feeder_connection_enabled===false?'Disabled':state.ble_connected===true?'Connected':state.feeder_connection_enabled===true?'Enabled · waiting for feeder':'Unknown';

}
async function pollLink(version){
  try{
    const r=await fetch('/admin/command',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Gateway-Console':'1'},body:new URLSearchParams({command:'status'})});
    if(!r.ok)throw new Error('Link status unavailable');
    const text=await r.text();
    const state=JSON.parse(text.slice(text.indexOf('{')));
    if(version!==viewVersion)return;
    updateLink(state);
  }catch(e){
    if(version!==viewVersion)return;
    updateLink({});
  }
  if(version===viewVersion)refreshTimer=setTimeout(()=>pollLink(version),1000);
}
function plainStatus(text){
  const start=text.indexOf('{');
  if(start<0)return text;
  const fields=JSON.parse(text.slice(start));
  const lines=Object.entries(fields).map(([key,value])=>{
    const label=key.replace(/_/g,' ').replace(/\b(ble|http|cccd)\b/g,word=>word.toUpperCase()).replace(/\bc\b/g,'C');
    const display=value===null?'unavailable':value===''?'none':String(value);
    return label.charAt(0).toUpperCase()+label.slice(1)+': '+display;
  });
  return text.slice(0,start)+lines.join('\n')+'\n';
}
let viewVersion=0, refreshTimer;
async function refresh(command,version,value){
  try{
    const r=await fetch('/admin/command',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Gateway-Console':'1'},body:new URLSearchParams(value===undefined?{command}:{command,value})});
    if(version!==viewVersion)return;
    if(r.status===401){location.replace('/login');return;}
    const text=await r.text();
    if(version!==viewVersion)return;
    const showStatus=['status','open','close','feeder connect','feeder disconnect',].includes(command);
    output.textContent=showStatus?plainStatus(text):text;
    if(showStatus && r.ok){
      const start=text.indexOf('{');
      if(start>=0)updateLink(JSON.parse(text.slice(start)));
    }
    if(!showStatus)pollLink(version);
    if(showStatus)refreshTimer=setTimeout(()=>refresh('status',version),1000);
  }catch(e){
    if(version!==viewVersion)return;
    output.textContent='Request failed: '+e.message;
    if(['status','open','close','feeder connect','feeder disconnect',].includes(command))refreshTimer=setTimeout(()=>refresh('status',version),3000);
  }
}
function run(command,value){
  if(command==='reboot'&&!confirm('Reboot the gateway now?'))return;
  clearTimeout(refreshTimer);
  const version=++viewVersion;

  return refresh(command,version,value);
}
document.querySelectorAll('button[data-command]').forEach(button=>{button.onclick=()=>run(button.dataset.command);});run('status');
</script></html>)HTML";

const char updatePage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Update feeder gateway</title>
<body><form method="POST" action="/logout"><button>Log Out</button></form><h1>Update gateway firmware</h1><p>Select the application file <code>feeder_gateway.ino.bin</code>.
Do not upload the merged, bootloader or partition-table binary. Keep power connected during upload.</p>
<form method="POST" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin" required><button>Upload and reboot</button></form>
<p><a href="/">Back to console</a></p></body></html>)HTML";

bool uploadStarted = false;
bool uploadComplete = false;
bool uploadRejected = false;
String uploadError;

void adminRoutes() {
  sessionStoreReady = sessionStore.begin("feeder-admin", false);
  if (sessionStoreReady) {
    for (unsigned i = 0; i < 4; ++i) adminSessions[i] = sessionStore.getString(("s" + String(i)).c_str(), "");
    nextSession = sessionStore.getUInt("next", 0) % 4;
  }
  server.on("/login", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", loginPage);
  });
  server.on("/login", HTTP_POST, []() {
    server.sendHeader("Cache-Control", "no-store");
    if (!sameOrigin()) { server.send(403, "text/plain", "Invalid login origin"); return; }
    if (!sessionStoreReady) { server.send(503, "text/plain", "Session storage unavailable"); return; }
    if (!kAdminPassword[0] || String(kAdminPassword) == "CHANGE-ME" ||
        server.arg("username") != kAdminUser || server.arg("password") != kAdminPassword) {
      server.send(401, "text/html", "Incorrect username or password. <a href='/login'>Try again</a>"); return;
    }
    uint8_t bytes[32]; esp_fill_random(bytes, sizeof(bytes));
    char encoded[65];
    for (unsigned i = 0; i < sizeof(bytes); ++i) snprintf(encoded + i * 2, 3, "%02x", bytes[i]);
    int slot = sessionIndex();
    if (slot < 0) {
      for (int i = 0; i < 4; ++i) if (!adminSessions[i].length()) { slot = i; break; }
    }
    if (slot < 0) slot = nextSession;
    if (!sessionStore.putString(("s" + String(slot)).c_str(), encoded)) {
      server.send(503, "text/plain", "Unable to save session"); return;
    }
    adminSessions[slot] = encoded;
    nextSession = (slot + 1) % 4; sessionStore.putUInt("next", nextSession);
    server.sendHeader("Set-Cookie", "feeder_session=" + String(encoded) + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=34560000");
    server.sendHeader("Location", "/"); server.send(303, "text/plain", "Logged in");
  });
  server.on("/logout", HTTP_POST, []() {
    server.sendHeader("Cache-Control", "no-store");
    if (!sameOrigin()) { server.send(403, "text/plain", "Invalid logout origin"); return; }
    int slot = sessionIndex();
    if (slot >= 0) {
      if (!sessionStore.remove(("s" + String(slot)).c_str())) {
        server.send(503, "text/plain", "Unable to clear session; please retry"); return;
      }
      adminSessions[slot] = "";
    }
    server.sendHeader("Set-Cookie", "feeder_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    server.sendHeader("Location", "/login"); server.send(303, "text/plain", "Logged out");
  });
  server.on("/", HTTP_GET, []() {
    if (!requireAdmin()) return;
    server.send_P(200, "text/html", adminPage);
  });
  server.on("/admin/command", HTTP_POST, []() {
    if (!requireAdmin()) return;
    // Browsers cannot send this custom header cross-origin without a preflight,
    // which this server does not grant. Curl can use it without an Origin.
    if (server.header("X-Gateway-Console") != "1" ||
        (server.hasHeader("Origin") && !sameOrigin())) {
      server.send(403, "text/plain", "Invalid console origin/header"); return;
    }
    String cmd = server.arg("command"); cmd.trim(); cmd.toLowerCase();
    if (cmd == "status") server.send(200, "text/plain", diagnostics());
    else if (cmd == "help") server.send(200, "text/plain", "status: network, memory, OTA and feeder state\nlogs: last 32 events (volatile)\nconfig: current configuration\nopen / close: send feeder request\nfeeder disconnect / feeder connect: save feeder-only Bluetooth connection preference\nreboot: restart gateway\nBLE automatically discovers F-80 and pins its address.\n");
    else if (cmd == "logs") {
      String logs;
      for (unsigned i = 0; i < logCount; ++i) logs += recentLogs[(logNext + 32 - logCount + i) % 32] + "\n";
      server.send(200, "text/plain", logs);
    } else if (cmd == "config") {
      server.send(200, "text/plain", "Hostname: " + String(kHostname) + "\nNetwork: Ethernet DHCP\nWi-Fi: disabled\nBLE: GO feeder\nIndigo API key: " + String(kApiKey[0] ? "configured" : "not configured") + "\nConfiguration is compiled into firmware; use OTA to change it.\n");
    } else if (cmd == "feeder disconnect" || cmd == "feeder connect") {
      const bool connect = cmd == "feeder connect";
      if(!feeder.setConnectionEnabled(connect)) {
        server.send(503, "text/plain", "Unable to save feeder connection preference"); return;
      }
      logEvent(connect ? "Feeder Bluetooth reconnect enabled [web]" : "Feeder Bluetooth disconnect requested [web]; automatic reconnect disabled");
      server.send(202, "application/json", statusJson());
    } else if (cmd == "open" || cmd == "close") {
      bool accepted = performCommand(cmd == "open", "web");
      server.send(accepted ? 202 : 503, "application/json", statusJson());
    } else if (cmd == "reboot") {
      logEvent("Administrator requested reboot");
      server.send(200, "text/plain", "Rebooting. Reload the console in a few seconds.");
      rebootAt = millis() + 1000;
    } else server.send(400, "text/plain", "Unknown command. Use help.");
  });
  server.on("/update", HTTP_GET, []() {
    if (!requireAdmin()) return;
    server.send_P(200, "text/html", updatePage);
  });
  server.on("/update", HTTP_POST, []() {
    bool ready = uploadComplete && !uploadRejected && !uploadError.length();
    String error = uploadError;
    uploadStarted = uploadComplete = uploadRejected = false;
    uploadError = "";
    if (!requireAdmin()) { Update.abort(); return; }
    if (!sameOrigin()) { Update.abort(); server.send(403, "text/plain", "Invalid upload origin"); return; }
    if (ready && !Update.end(true)) { ready = false; error = Update.errorString(); }
    if (!ready) {
      Update.abort();
      logEvent("OTA rejected: " + error);
      server.send(400, "text/plain", "Update failed: " + (error.length() ? error : String("No complete firmware image received")));
    } else {
      logEvent("OTA verified; reboot scheduled");
      server.send(200, "text/plain", "Firmware uploaded and verified. Rebooting; reload / in a few seconds.");
      rebootAt = millis() + 1000;
    }
  }, []() {
    // WebServer also invokes this callback for raw POST bodies. upload() is
    // only valid for multipart requests; otherwise its pointer can be null.
    if (!server.header("Content-Type").startsWith("multipart/form-data")) return;
    HTTPUpload &u = server.upload();
    if (u.status == UPLOAD_FILE_START) {
      if (!adminAuthenticated() || !sameOrigin()) { uploadRejected = true; return; }
      if (uploadStarted) { uploadRejected = true; uploadError = "Only one image per request is allowed"; Update.abort(); return; }
      uploadStarted = true; uploadComplete = false; uploadError = "";
      if (u.name != "firmware" || !u.filename.endsWith(".bin")) {
        uploadRejected = true; uploadError = "Expected a firmware .bin file"; return;
      }
      logEvent("OTA upload started");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) uploadError = Update.errorString();
    } else if (u.status == UPLOAD_FILE_WRITE && uploadStarted && !uploadRejected && !uploadError.length()) {
      if (Update.write(u.buf, u.currentSize) != u.currentSize) uploadError = Update.errorString();
    } else if (u.status == UPLOAD_FILE_END && uploadStarted && !uploadRejected && !uploadError.length()) {
      // Finalization is deferred until the request ends, allowing rejection of multiple files.
      uploadComplete = true;
    } else if (u.status == UPLOAD_FILE_ABORTED) {
      Update.abort(); uploadStarted = uploadComplete = uploadRejected = false;
      uploadError = "Upload aborted"; logEvent(uploadError);
    }
  });
}
