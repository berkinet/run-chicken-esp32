# Build, flash and commission

## Toolchain

Use Arduino CLI and **esp32 by Espressif Systems 3.3.0**, or Arduino IDE with the
same core. Only libraries bundled with that core are needed. Commands below use
zsh; install Arduino CLI from its official distribution first.

```zsh
arduino-cli config init  # only if you do not already have a CLI config
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.0 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
cp firmware/feeder_gateway/admin_credentials.example.h firmware/feeder_gateway/admin_credentials.h
cp firmware/feeder_gateway/remote_states_credentials.example.h firmware/feeder_gateway/remote_states_credentials.h
```

Edit `admin_credentials.h` to set a unique nonempty admin password. Edit
`config.h` to set a shared `kApiKey` if API authentication is desired. An empty
API key permits unauthenticated LAN API requests; the browser login is separate.
Local credential headers are ignored by Git. Keep `kStateEndpoint` empty to disable
Indigo publication. If enabled, set a unique source ID and shared token as described
in [Indigo setup](../indigo/README.md). No receiver is needed for normal operation.

```zsh
zsh firmware/test.zsh
zsh firmware/build.zsh
```

The build script accepts optional `ARDUINO_CLI` (executable path) and
`ARDUINO_CONFIG` (CLI YAML path). It writes to ignored `build/`.

## First USB flash

Select the **ESP32S3 Dev Module**, USB CDC On Boot enabled, 16 MB flash, OPI PSRAM,
and Custom partition scheme. `partitions.csv` supplies two 6.25 MiB OTA slots.
The compile FQBN is embedded in `firmware/build.zsh`. Verify these settings against
your physical board. Never assume a different board has the same pin mapping.

Use `arduino-cli board list` to identify your board's serial port. Do not select
an unrelated automation interface. Replace `YOUR_SERIAL_PORT` below:

```zsh
arduino-cli upload --port YOUR_SERIAL_PORT \
  --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom \
  --input-dir build firmware/feeder_gateway
```

Use the board manufacturer's bootloader procedure if automatic upload fails.
The first USB flash establishes the partition layout. Open the serial monitor at
115200 baud or check DHCP leases to find the assigned Ethernet address. Reserve
that address. Hostname is `chicken-feeder`; mDNS is not implemented.

## Commissioning

1. Power the feeder normally, place the ESP32 in radio range, connect Ethernet.
2. Close the phone app so it releases the feeder connection. Have only the target
   F-80 advertising during initial enrollment; multiple matches are rejected.
3. Visit `http://GATEWAY-IP/` and log in as `admin` using your configured password.
4. Check Ethernet, `ble_connected`, and `clock_synced`. The gateway obtains UTC
   from pool.ntp.org/time.cloudflare.com. Commands are rejected without valid time.
5. Watch the feeder with the hatch clear. Request Open, then Close. HTTP acceptance
   and a BLE write acknowledgement do not by themselves confirm final position.
   Verify `last_result=confirmed` and `physical_state=open/closed` plus actual motion.
6. Disconnect/reconnect through Management to verify recovery. No movement request
   is replayed on reconnection or reboot. State may remain unknown until a position
   report is received.

The selected address is saved in NVS namespace `feeder-ble`, key `address`.
There is no enrollment-reset button. To replace the feeder, back up configuration
and deliberately clear that key using a maintenance sketch, or perform a full
flash erase and recommission. Full erase also loses preferences and sessions.
Do not erase a working installation casually.

## Ethernet OTA

Build with the same core/partition settings. Upload only
`build/feeder_gateway.ino.bin` through Management → Upload firmware.
Do not upload a merged image, bootloader, or partition table there. Keep power
connected. A verified application is written to the alternate slot and rebooted.
The classic Arduino build is not a promise of automatic rollback after a crashing
image; USB recovery remains necessary. Changes to the partition layout require USB.

Browser sessions persist across restart, have no idle timeout and use
HttpOnly/SameSite=Strict cookies; HTTP itself remains unencrypted. Log Out revokes
the session. Four sessions are retained; a fifth replaces the oldest.
