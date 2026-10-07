#pragma once

// Waveshare ESP32-S3-ETH / ESP32-S3-POE-ETH (not the 8DI/8DO variant).
constexpr int kEthMiso = 12;
constexpr int kEthMosi = 11;
constexpr int kEthClock = 13;
constexpr int kEthCs = 14;
constexpr int kEthReset = 9;
constexpr int kEthInterrupt = 10;
constexpr char kHostname[] = "chicken-feeder";
constexpr unsigned short kHttpPort = 80;

// Optional shared token, sent as X-API-Key. Empty allows trusted-LAN testing.
// HTTP is unencrypted; keep this gateway on your local network.
constexpr char kApiKey[] = "";
