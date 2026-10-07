# RUN-CHICKEN GO Feeder: local ESP32 gateway

Control a RUN-CHICKEN GO Feeder over Bluetooth from an Ethernet-connected
ESP32-S3. This repository contains the feeder firmware, protocol details, a
reproduction guide, and optional Indigo automation. It does not require a cloud
service or Home Assistant. NTP is used to obtain timestamps for feeder commands.

The tested feeder was sold as **GO Feeder — Red, RC-GO-RED-BT** and advertised
its Bluetooth name as **F-80**. The owner confirmed successful operation in the
coop on 6 October 2026. This is evidence for that unit, not a compatibility promise
for every product carrying the GO name or future firmware.

This is a feeder-only extraction of a working installation. The standalone build
uses Arduino ESP32 3.3.0 and the original feeder scan/reconnect path. It has been
compiled and tested offline after extraction; this exact extracted image has not
been flashed onto the installed gateway. See [validation](docs/validation.md).

## What it does

- Discovers one F-80 feeder, pins its address, and reconnects automatically.
- Sends explicit Open/Close commands and checks fresh position reports.
- Provides a browser console, blue current-position buttons, connection controls,
  diagnostics, persistent login sessions, and Ethernet application OTA.
- Offers a small HTTP API usable from Indigo, scripts, or another controller.
- Optionally publishes changed state and event logs to an Indigo receiver.

It does not add feeder firmware updates, feeding schedules, battery decoding,
RFID hardware support inside the ESP32, or obstruction sensing. The ESP32 does
not connect to the feeder motor wiring. The feeder keeps its own power supply.

## Start here

1. Check the [bill of materials and wiring](docs/hardware.md).
2. Follow [build, first flash, and commissioning](docs/setup.md).
3. Use the [HTTP API](docs/api.md) or [optional Indigo integration](indigo/README.md).
4. Read [protocol and evidence](docs/protocol.md) and [lessons/troubleshooting](docs/learnings.md).

Keep the gateway on a trusted LAN. HTTP is not encrypted. Configure a unique
admin password and API key before relying on network control. Check hatch
clearance and supervise first movement tests; software confirmation is not an
obstruction detector or an animal-safety guarantee.

## Development

Run `zsh firmware/test.zsh` for protocol, command-queue, and Indigo receiver tests.
Run `zsh firmware/build.zsh` after installing the pinned core and creating the
local credential headers. The optional publisher is disabled by default.
No original credentials, Bluetooth addresses, packet-capture files, app binaries,
or compiled firmware images are distributed.

[Attribution](THIRD_PARTY_NOTICES.md). RUN-CHICKEN and Waveshare are product names;
this is an independent project, not vendor firmware or an endorsed product.
