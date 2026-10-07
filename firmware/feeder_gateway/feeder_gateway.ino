#include <Arduino.h>
#include <ETH.h>
#include <SPI.h>
#include <WebServer.h>
#include <atomic>
#include "config.h"
#include "FeederTransport.h"
#include "Temperature.h"
#include "RemoteStates.h"
RemoteStates remoteStates;

WebServer server(kHttpPort);
FeederTransport feeder;
std::atomic<bool> ethernetReady{false};
bool serverStarted = false;
const char* requestedState = "unknown";
const char* lastResult = "none";
uint32_t commandCount = 0;
uint32_t lastCommandMs = 0;

void onNetworkEvent(arduino_event_id_t event, arduino_event_info_t info) {
  // Network callbacks run on a separate task; HTTP/state updates stay in loop().
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(kHostname);
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ethernetReady.store(true);
      Serial.printf("Ethernet IP: %s\n", ETH.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      ethernetReady.store(false);
      break;
    default:
      break;
  }
}

bool authorized() {
  if (kApiKey[0] == '\0' || server.header("X-API-Key") == kApiKey) return true;
  server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

String statusJson() {
  String json = "{\"mode\":\"ble\",\"simulated\":false,\"ethernet_connected\":";
  json += ethernetReady.load() ? "true" : "false";
  json += ",\"ble_connected\":";
  json += feeder.connected() ? "true" : "false";
  json += feeder.details();
  json += ",\"physical_state\":\"" + String(feeder.state()) + "\",\"requested_state\":\"";
  json += requestedState;
  json += "\",\"last_result\":\"";
  json += String(lastResult)=="rejected" ? "rejected" : feeder.outcome();
  json += "\",\"command_count\":" + String(commandCount);
  json += ",\"last_command_ms\":";
  json += commandCount ? String(lastCommandMs) : String("null");
  json += temperatureJson();
  json += remoteStates.details();
  json += ",\"uptime_ms\":" + String(millis()) + "}";
  return json;
}

void logEvent(const String &message);

bool performCommand(bool open, const char* source) {
  lastCommandMs = millis();
  ++commandCount;
  const bool accepted = feeder.send(open);
  if (accepted) requestedState = open ? "open" : "closed";
  lastResult = accepted ? "queued" : "rejected";
  logEvent(String(open ? "OPEN" : "CLOSE") + " request [" + source + "]: " + lastResult);
  return accepted;
}

void command(bool open) {
  if (!authorized()) return;
  const bool accepted = performCommand(open, "API");
  server.sendHeader("Cache-Control", "no-store");
  server.send(accepted ? 202 : 503, "application/json", statusJson());
}

#include "Admin.h"


void setup() {
  Serial.begin(115200);
  startTemperature();
  // Do not wait for a serial terminal: this gateway runs unattended over PoE.
  Serial.println("Chicken feeder gateway: BLE mode");
  const char* headers[] = {"X-API-Key", "Origin", "X-Gateway-Console", "Content-Type", "Cookie"};
  server.collectHeaders(headers, 5);
  adminRoutes();
  logEvent("Gateway started; real BLE commands enabled");
  server.on("/api/v1/status", HTTP_GET, []() {
    if (!authorized()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", statusJson());
  });
  server.on("/api/v1/open", HTTP_POST, []() { command(true); });
  server.on("/api/v1/close", HTTP_POST, []() { command(false); });
  server.onNotFound([]() {
    if (server.uri() == "/api/v1/open" || server.uri() == "/api/v1/close" ||
        server.uri() == "/api/v1/status") {
      server.sendHeader("Allow", server.uri() == "/api/v1/status" ? "GET" : "POST");
      server.send(405, "application/json", "{\"error\":\"method_not_allowed\"}");
    } else {
      server.send(404, "application/json", "{\"error\":\"not_found\"}");
    }
  });
  feeder.begin();
  remoteStates.begin();
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  Network.onEvent(onNetworkEvent);
  SPI.begin(kEthClock, kEthMiso, kEthMosi);
  if (!ETH.begin(ETH_PHY_W5500, 1, kEthCs, kEthInterrupt, kEthReset, SPI)) {
    Serial.println("ERROR: Ethernet initialization failed; check board/pins and restart");
  }
}

void loop() {
  pollTemperature();
  static String previousOutcome;
  String currentOutcome = feeder.outcome();
  if(currentOutcome != previousOutcome) {
    previousOutcome = currentOutcome;
    logEvent("BLE result: " + currentOutcome + "; position: " + feeder.state());
  }
  static String previousPosition, previousConnection;
  String position = feeder.state();
  String connection = feeder.connected() ? "connected" : "disconnected";
  if (position != previousPosition) {
    logEvent("Observed door position: " + previousPosition + " -> " + position +
             "; requested: " + requestedState + "; result: " + feeder.outcome());
    previousPosition = position;
  }
  if (connection != previousConnection) {
    logEvent("BLE " + connection);
    previousConnection = connection;
  }
  // Query diagnostics after USB reconnects without resetting the gateway.
  if (Serial.available() && Serial.read() == '?') {
    Serial.printf("Ethernet link: %s, IP: %s, MAC: %s\n",
                  ETH.linkUp() ? "up" : "down",
                  ETH.localIP().toString().c_str(), ETH.macAddress().c_str());
    Serial.println(diagnostics());
  }
  const bool ready = ethernetReady.load();
  if (ready && !serverStarted) {
    server.begin();
    serverStarted = true;
    logEvent("HTTP server ready at " + ETH.localIP().toString());
  } else if (!ready && serverStarted) {
    server.stop();
    serverStarted = false;
    logEvent("Ethernet lost; HTTP stopped");
  }
  if (serverStarted) server.handleClient();
  static uint32_t stateSampleMs = 0;
  if (static_cast<uint32_t>(millis() - stateSampleMs) >= 250) {
    stateSampleMs = millis();
    String fields[9] = {
      String("\"physical_state\":\"") + feeder.state() + "\"",
      String("\"requested_state\":\"") + requestedState + "\"",
      String("\"last_result\":\"") + (String(lastResult)=="rejected" ? "rejected" : feeder.outcome()) + "\"",
      String("\"ble_connected\":") + (feeder.connected() ? "true" : "false"),
      String("\"ble_busy\":") + (feeder.isBusy() ? "true" : "false"),
      String("\"ble_error\":\"") + feeder.lastError() + "\"",
      String("\"chip_temperature_c\":") + temperatureNumber(chipTemperature),
      String("\"chip_temperature_min_c\":") + temperatureNumber(chipTemperatureMin),
      String("\"chip_temperature_max_c\":") + temperatureNumber(chipTemperatureMax)
    };
    remoteStates.offer(fields, ready);
  }
  if (rebootAt && static_cast<int32_t>(millis() - rebootAt) >= 0) ESP.restart();
  delay(2);
}
