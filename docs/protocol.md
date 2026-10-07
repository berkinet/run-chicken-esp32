# GO feeder BLE protocol: implemented subset

These details combine official-app packet analysis, a working-app capture on
2 October 2026, and physical operation confirmed by the owner. They are not an
official RUN-CHICKEN API specification. No vendor app binaries or disassembly
are distributed here.

## Discovery and GATT

The tested GO Feeder advertises `F-80`. Firmware scans actively for three seconds,
requires exactly one match on first enrollment, then pins the address. Later scans
match that address. It does not rely on service attribute handles.

| Purpose | UUID |
|---|---|
| Write service | `00000000-cc7a-482a-984a-7f2ed5b3e58f` |
| Write characteristic | `00000000-8e22-4541-9d4c-21edae82ed19` |
| Read/notify service | `0000004f-cc7a-482a-984a-7f2ed5b3e58f` |
| Read/notify characteristic | `00000001-8e22-4541-9d4c-21edae82ed19` |

Subscribe to notifications and resolve UUIDs for each connection. The gateway
requests MTU 185 and uses writes with response. It sends a request/init packet
once connected if its clock is valid, and also reads the status characteristic
when available. No custom application authentication or pairing exchange is
implemented. Release phone-app connections when testing gateway access.

## 32-byte transmitted packet

| Byte(s), zero-based | Value |
|---|---|
| 0 | `00` for movement; `01` for request/init |
| 1–8 | Unix seconds, unsigned 64-bit little-endian |
| 9–20 | Zero |
| 21 | Open `01`, Close `02`; request/init uses zero |
| 22–30 | Zero |
| 31 | CRC-8 of bytes 0–30, polynomial `0x07`, initial zero, no final XOR |

The implementation is in `FeederProtocol.h`. Captured vectors used by its tests:

```text
OPEN  00 31 94 bf 6a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 2e
CLOSE 00 33 94 bf 6a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00 00 00 0b
```

These historical timestamps are test vectors, not packets to replay against a
feeder today. Always generate a current timestamp.

## Position reports

The observed captured notification values were 153 bytes. The implemented decoder
only needs 18 bytes: byte 0 must be `0x81`, byte 15 must be zero (final state), and
byte 17 is zero for Open or one for Closed. Other prefixes (for example `0x48`)
are not position reports. Moving, malformed and unrecognized reports do not
establish a completed position. The decoder does not validate an incoming CRC;
it is a deliberately narrow interpretation of the observed report fields.

The gateway invalidates position before a command, records its incoming sequence,
and waits for a fresh matching position report. ATT Write Response is not
completion. The captured app OPEN write acknowledgement arrived around 59 ms
later, but its final report arrived about 2.16 seconds after the write; CLOSE
showed a similar acknowledgement followed by final report about 1.95 seconds later.

## Why not simply use T50 packets?

The studied Home Assistant implementation has different T50 and GIANT packet
layouts. The recovered official-app control routine and our GO capture use the
layout above, which matches its GIANT-style layout at present-day timestamps.
That name does not make the tested device a GIANT. Do not generalize GO behavior
to T50, T80, doors, older firmware, or other feeders without captures and tests.

Research reference: [ha-run-chicken protocol at the reviewed commit](https://github.com/bkanuka/ha-run-chicken/blob/6bdaa25f3a9530187ef4e7374bb70c9ae1a4f585/custom_components/run_chicken/run_chicken_ble/protocol.py).
