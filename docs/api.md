# HTTP API

All paths use port 80 on the gateway's LAN address. Send `X-API-Key` when configured.

| Method | Path | Meaning |
|---|---|---|
| GET | `/api/v1/status` | State, command result, BLE/network health and chip temperature |
| POST | `/api/v1/open` | Request opening, empty body |
| POST | `/api/v1/close` | Request closing, empty body |

Movement requests return 202 when accepted, 503 if unavailable (including disabled
BLE or missing clock). Wrong methods return 405, unknown paths 404, wrong configured
API key 401. A 202 is **acceptance**, not physical completion. Poll status.

```zsh
curl -H 'X-API-Key: YOUR_KEY' http://GATEWAY-IP/api/v1/status
# These next commands move the feeder. Supervise them.
curl -X POST -H 'X-API-Key: YOUR_KEY' http://GATEWAY-IP/api/v1/open
curl -X POST -H 'X-API-Key: YOUR_KEY' http://GATEWAY-IP/api/v1/close
```

Important fields:

- `physical_state`: `open`, `closed`, or `unknown`; based on feeder reports.
- `requested_state`: last accepted desired state, not an observation.
- `last_result`: `none`, `queued`, `sending`, `confirmed`, `failed`, or `rejected`.
- `ble_busy`, `ble_connected`, `feeder_connection_enabled`, `ble_error`.
- `clock_synced`, `ethernet_connected`, `uptime_ms`.

There is one active command and one replaceable pending target. A new request
while busy replaces the pending target rather than creating an unbounded queue.
Disconnect clears active/pending work. A failed command is not automatically retried.
The confirmation deadline is 12 seconds. The API is not a safety interlock.

Browser administration uses a separate login cookie. The console provides status,
logs, config, Open/Close, feeder Connect/Disconnect and reboot. Connect/Disconnect
persists the enabled preference. Config is diagnostic; firmware configuration
values require a rebuild. The RAM log holds the last 32 events and resets on boot.
