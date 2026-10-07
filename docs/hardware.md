# Bill of materials and wiring

## Required gateway and feeder

| Qty | Part | Notes |
|---|---|---|
| 1 | RUN-CHICKEN GO Feeder, RC-GO-RED-BT | Tested unit advertises F-80. Retain manufacturer power arrangement. |
| 1 | Waveshare ESP32-S3-ETH with PoE module (ESP32-S3-POE-ETH), 16 MB flash / 8 MB PSRAM | W5500 Ethernet board. Do not substitute the similarly named 8DI/8DO board using these pins. |
| 1 | Compatible PoE switch port or injector | Match the exact board/module's power requirements. |
| 1 | Ethernet cable | Connect the gateway to the LAN and PoE supply. |
| 1 | USB data cable | First flash and recovery; select the board's programming connector. |
| 1 | Protective enclosure and mounting hardware | Protect against coop dust, moisture, pecking and cable strain while preserving radio reception. |

No cost estimate is supplied: vendor bundles and prices vary. Verify board
revision, power requirements and connector arrangement against the supplied
Waveshare documentation before connecting power. This guide assumes the assembled
Ethernet/PoE board; it does not specify custom mains wiring.

The firmware's W5500 SPI pin map:

| Function | ESP32 GPIO |
|---|---:|
| MISO | 12 |
| MOSI | 11 |
| Clock | 13 |
| Chip select | 14 |
| Reset | 9 |
| Interrupt | 10 |

## Optional RFID / Indigo installation

The original automation also used an Indigo server, a Phidgets 1024 RFID reader
with the Phidgets22 plugin, and 125 kHz TK4100/EM4100-compatible poultry leg tags
(advertised 19–21 mm rings). These are **not required** for browser/API control.
The ESP32 contains no RFID reader code; Indigo turns presence events into feeder
requests. Reader wiring, tag compatibility, fit, antenna coverage and plugin
setup must be checked independently. Do not infer a particular hub requirement
or tested reader revision from the original order list.

A DS18B20 was an additional installation accessory, but is omitted here to keep
the firmware limited to the feeder. The built-in chip temperature is still
reported as gateway diagnostics; it is not coop air temperature.
