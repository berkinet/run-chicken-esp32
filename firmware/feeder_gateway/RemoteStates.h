#pragma once
#include <HTTPClient.h>
#include <atomic>
#include <esp_random.h>
#include "remote_states_credentials.h"

// A single overwritten queue keeps network I/O off the HTTP/BLE tasks.
// The worker retries the newest snapshot, never a backlog of old states.
class RemoteStates {
 public:
  void event(const String &message) {
    if (!boot.length()) boot = String(esp_random(), HEX) + String(esp_random(), HEX);
    String escaped;
    for (unsigned i=0; i<message.length() && i<180; ++i) {
      char c=message[i];
      if (c=='"' || c=='\\') escaped += '\\';
      if (static_cast<unsigned char>(c)>=32) escaped += c;
    }
    events[eventNext] = String("{\"id\":\"") + boot + "-" + String(++eventSequence) +
      "\",\"message\":\"" + String(millis()) + " ms: " + escaped + "\"}";
    eventNext = (eventNext+1)%16;
    if (eventCount<16) ++eventCount;
  }
  void begin() {
    if (!kStateEndpoint[0]) return;
    queue = xQueueCreate(1, sizeof(Snapshot));
    if (queue && xTaskCreate(worker, "remote-states", 40960, this, 1, nullptr) == pdPASS) return;
    if (queue) { vQueueDelete(queue); queue = nullptr; }
    lastHttp = -1000;
  }
  void offer(const String (&fields)[9], bool online) {
    if (!queue) return;
    Snapshot &snapshot = offered;
    for (unsigned i = 0; i < 9; ++i) {
      if (fields[i].length() >= sizeof(snapshot.fields[i])) return;
      fields[i].toCharArray(snapshot.fields[i], sizeof(snapshot.fields[i]));
    }
    String journal = "[";
    for (unsigned i=0; i<eventCount; ++i) {
      if(i) journal += ',';
      journal += events[(eventNext+16-eventCount+i)%16];
    }
    journal += ']';
    if(journal.length() >= sizeof(snapshot.events)) return;
    journal.toCharArray(snapshot.events, sizeof(snapshot.events));
    snapshot.uptime = millis();
    snapshot.online = online;
    xQueueOverwrite(queue, &snapshot);
  }
  String details() const {
    return String(",\"state_publish_http\":") + lastHttp.load() +
      ",\"state_publish_count\":" + sent.load();
  }
 private:
  String boot, events[16];
  unsigned eventNext=0, eventCount=0, eventSequence=0;
  struct Snapshot { char events[8192]; char fields[9][160]; uint32_t uptime; bool online; };
  Snapshot offered{}; // Main loop stack is too small for the event journal.
  QueueHandle_t queue = nullptr;
  std::atomic<int> lastHttp{0};
  std::atomic<unsigned> sent{0};
  static void worker(void* arg) { static_cast<RemoteStates*>(arg)->run(); }
  void run() {
    Snapshot delivered{};
    bool haveDelivered = false;
    uint32_t lastSuccess = 0, lastAttempt = 0;
    bool attempted = false, failed = false;
    for (;;) {
      Snapshot snapshot{};
      if (xQueueReceive(queue, &snapshot, portMAX_DELAY) != pdTRUE || !snapshot.online) continue;
      const uint32_t now = millis();
      if (attempted && static_cast<uint32_t>(now - lastAttempt) < (failed ? 5000U : 250U)) continue;
      String delta;
      for (unsigned i = 0; i < 9; ++i) {
        if (!haveDelivered || failed || strcmp(delivered.fields[i], snapshot.fields[i]) != 0) {
          if (delta.length()) delta += ',';
          delta += snapshot.fields[i];
        }
      }
      if (!delta.length() && haveDelivered && strcmp(delivered.events, snapshot.events)==0 && static_cast<uint32_t>(now - lastSuccess) < 30000U) continue;
      if (delta.length()) delta += ',';
      attempted = true;
      lastAttempt = now;
      String body = String("{\"version\":1,\"source\":\"") + kStateSource +
        "\",\"states\":{" + delta + "\"uptime_ms\":" + snapshot.uptime + "},\"events\":" + snapshot.events + "}";
      NetworkClient client;
      HTTPClient http;
      http.setConnectTimeout(1500);
      http.setTimeout(1500);
      int code = -1;
      if (http.begin(client, kStateEndpoint)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-Remote-States-Key", kStateToken);
        code = http.POST(body);
        http.end();
      }
      lastHttp = code;
      failed = code != 200;
      if (!failed) {
        delivered = snapshot;
        haveDelivered = true;
        lastSuccess = millis();
        ++sent;
      }
    }
  }
};
